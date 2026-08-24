#nullable enable

using System.Threading;

namespace Chimera.Client.Common
{
	public sealed class WebSocketServer
	{
		public ClientWebSocketWrapper Open(
			Uri uri,
			CancellationToken cancellationToken = default/* == CancellationToken.None */)
				=> new(uri, cancellationToken);
	}
}
