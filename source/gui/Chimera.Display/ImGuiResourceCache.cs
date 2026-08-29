using System.Collections.Generic;
using System.Drawing;
using System.Numerics;

namespace Chimera.Display
{
	/// <summary>
	/// Shared resource cache for the ImGui 2D renderer
	/// This allows multiple ImGui renderers to share the same cache
	/// </summary>
	public sealed class ImGuiResourceCache
	{
		private readonly IGL _igl;

		internal readonly IPipeline Pipeline;
		internal readonly Dictionary<Bitmap, ITexture2D> TextureCache = [ ];

		internal readonly SolidBrush CachedBrush = new(default);

		public ImGuiResourceCache(IGL igl)
		{
			_igl = igl;

			var vertexLayoutItems = new PipelineCompileArgs.VertexLayoutItem[3];
			vertexLayoutItems[0] = new("aPosition", 2, 0, AttribUsage.Position);
			vertexLayoutItems[1] = new("aTexcoord", 2, 8, AttribUsage.Texcoord0);
			vertexLayoutItems[2] = new("aColor", 4, 16, AttribUsage.Color0, Integer: true);

			var compileArgs = new PipelineCompileArgs(
				vertexLayoutItems,
				vertexShaderArgs: new(ImGuiVertexShader_gl, "vsmain"),
				fragmentShaderArgs: new(ImGuiPixelShader_gl, "psmain"),
				fragmentOutputName: "FragColor");
			Pipeline = igl.CreatePipeline(compileArgs);

			igl.BindPipeline(Pipeline);
			SetTexture(null);
			SetBlendingParamters(null, true);
			igl.BindPipeline(null);
		}

		internal void SetProjection(int width, int height)
		{
			var projection = _igl.CreateGuiViewMatrix(width, height) * _igl.CreateGuiProjectionMatrix(width, height);
			Pipeline.SetUniformMatrix("um44Projection", ref projection);
		}

		internal void SetTexture(ITexture2D texture2D)
		{
			Pipeline.SetUniform("uSamplerEnable", texture2D != null);
			Pipeline.SetUniformSampler("uSampler0", texture2D);
		}

		internal void SetBlendingParamters(ITexture2D secondaryTexture, bool doBlendPass)
		{
			Pipeline.SetUniformSampler("uSampler1", secondaryTexture);
			Pipeline.SetUniform("uBlendEnable", secondaryTexture != null);
			Pipeline.SetUniform("uBlendPass", doBlendPass);

			if (secondaryTexture != null)
			{
				Pipeline.SetUniform("uSamplerSize", new Vector2(secondaryTexture.Width, secondaryTexture.Height));
			}
		}

		public void Dispose()
		{
			foreach (var cachedTex in TextureCache.Values)
			{
				cachedTex.Dispose();
			}
			CachedBrush.Dispose();
			TextureCache.Clear();
			Pipeline?.Dispose();
		}

		public void ClearTextureCache()
		{
			foreach (var cachedTex in TextureCache.Values)
			{
				cachedTex.Dispose();
			}

			TextureCache.Clear();
		}

		// ReSharper disable UseRawString

		public const string ImGuiVertexShader_gl = @"
//opengl 3.2
#version 150
uniform mat4 um44Projection;

in vec2 aPosition;
in vec2 aTexcoord;
in vec4 aColor;

out vec2 vTexcoord0;
out vec4 vColor0;

void main()
{
	vec4 temp = vec4(aPosition,0,1);
	gl_Position = um44Projection * temp;
	vTexcoord0 = aTexcoord;
	vColor0 = aColor;
}";

		public const string ImGuiPixelShader_gl = @"
//opengl 3.2
#version 150
uniform bool uSamplerEnable, uBlendPass, uBlendEnable;
uniform vec2 uSamplerSize;
uniform sampler2D uSampler0, uSampler1;

in vec2 vTexcoord0;
in vec4 vColor0;

out vec4 FragColor;

void main()
{
	if (uBlendPass)
	{
		vec4 temp = vColor0;
		if(uSamplerEnable) temp *= texture(uSampler0, vTexcoord0);

		if (uBlendEnable)
		{
			if (temp.a != 1.0)
			{
				vec4 prev = texture(uSampler1, gl_FragCoord.xy / uSamplerSize);
				if (temp.a == 0.0)
				{
					temp = prev;
				}
				else
				{
					float alpha = prev.a + temp.a - (prev.a * temp.a);
					temp.r = ((temp.r * temp.a) + (prev.r * prev.a * (1.0 - temp.a))) / alpha;
					temp.g = ((temp.g * temp.a) + (prev.g * prev.a * (1.0 - temp.a))) / alpha;
					temp.b = ((temp.b * temp.a) + (prev.b * prev.a * (1.0 - temp.a))) / alpha;
					temp.a = alpha;
				}
			}
		}

		FragColor = temp;
	}
	else
	{
		FragColor = texture(uSampler1, gl_FragCoord.xy / uSamplerSize);
	}
}";
	}
}
