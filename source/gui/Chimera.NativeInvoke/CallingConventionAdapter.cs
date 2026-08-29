using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;

namespace Chimera.NativeInvoke
{
	/// <summary>
	/// create interop delegates and function pointers for a particular calling convention
	/// </summary>
	public interface ICallingConventionAdapter
	{
		/// <summary>
		/// Like Marshal.GetFunctionPointerForDelegate(), but wraps a thunk around the returned native pointer
		/// to adjust the calling convention appropriately
		/// </summary>
		IntPtr GetFunctionPointerForDelegate(Delegate d);

		/// <summary>
		/// Like Marshal.GetFunctionPointerForDelegate, but only the unmanaged thunk-to-thunk part, with no
		/// managed wrapper involved.  Called "arrival" because it is to be used when the foreign code is calling
		/// back into host code.
		/// </summary>
		IntPtr GetArrivalFunctionPointer(IntPtr p, InvokerParameterInfo pp, object lifetime);

		/// <summary>
		/// Like Marshal.GetDelegateForFunctionPointer(), but wraps a thunk around the passed native pointer
		/// to adjust the calling convention appropriately
		/// </summary>
		Delegate GetDelegateForFunctionPointer(IntPtr p, Type delegateType);

		/// <summary>
		/// Like Marshal.GetDelegateForFunctionPointer(), but only the unmanaged thunk-to-thunk part, with no
		/// managed wrapper involved.static  Called "departure" beause it is to be used when first leaving host
		/// code for foreign code.
		/// </summary>
		IntPtr GetDepartureFunctionPointer(IntPtr p, InvokerParameterInfo pp, object lifetime);
	}

	public static class CallingConventionAdapterExtensions
	{
		public static T GetDelegateForFunctionPointer<T>(this ICallingConventionAdapter a, IntPtr p)
			where T : Delegate
			=> (T) a.GetDelegateForFunctionPointer(p, typeof(T));
	}

	public sealed class InvokerParameterInfo
	{
		public Type ReturnType { get; }
		public IReadOnlyList<Type> ParameterTypes { get; }

		public InvokerParameterInfo(Type returnType, IEnumerable<Type> parameterTypes)
		{
			ReturnType = returnType;
			ParameterTypes = parameterTypes.ToList().AsReadOnly();
		}

		/// <exception cref="InvalidOperationException"><paramref name="delegateType"/> does not inherit <see cref="Delegate"/></exception>
		public InvokerParameterInfo(Type delegateType)
		{
			if (!typeof(Delegate).IsAssignableFrom(delegateType))
			{
				throw new InvalidOperationException("Must be a delegate type!");
			}

			var invoke = delegateType.GetMethod("Invoke")!;
			ReturnType = invoke.ReturnType;
			ParameterTypes = invoke.GetParameters().Select(p => p.ParameterType).ToList().AsReadOnly();
		}
	}

	public static class CallingConventionAdapters
	{
		internal sealed class NativeConvention : ICallingConventionAdapter
		{
			public IntPtr GetArrivalFunctionPointer(IntPtr p, InvokerParameterInfo pp, object lifetime)
				=> p;

			public Delegate GetDelegateForFunctionPointer(IntPtr p, Type delegateType)
				=> Marshal.GetDelegateForFunctionPointer(p, delegateType);

			public IntPtr GetDepartureFunctionPointer(IntPtr p, InvokerParameterInfo pp, object lifetime)
				=> p;

			public IntPtr GetFunctionPointerForDelegate(Delegate d)
				=> Marshal.GetFunctionPointerForDelegate(d);
		}

		/// <summary>
		/// native (pass-through) calling convention
		/// </summary>
		public static ICallingConventionAdapter Native { get; } = new NativeConvention();
	}
}
