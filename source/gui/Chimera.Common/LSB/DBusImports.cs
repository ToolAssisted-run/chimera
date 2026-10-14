using System.Runtime.InteropServices;

namespace Chimera.Common
{
	/// <summary>
	/// The parts of libdbus (the reference D-Bus library, on every Linux desktop) that the
	/// desktop portal's file chooser needs. D-Bus is the desktop's local message bus: a
	/// socket on this machine, never the network.
	/// </summary>
	/// <remarks>
	/// A <c>DBusMessageIter</c> is a struct of libdbus' own that the caller allocates and
	/// libdbus fills in; it is 72 bytes on x64, and <see cref="IterSize"/> is room to
	/// spare. Its memory must not move while libdbus holds it, so it is unmanaged.
	/// </remarks>
	public static class DBusImports
	{
		private const string DBUS = "libdbus-1.so.3";

		public const int IterSize = 128;

		public const int DBUS_BUS_SESSION = 0;

		public const int DBUS_TYPE_BYTE = 'y';
		public const int DBUS_TYPE_BOOLEAN = 'b';
		public const int DBUS_TYPE_UINT32 = 'u';
		public const int DBUS_TYPE_STRING = 's';
		public const int DBUS_TYPE_OBJECT_PATH = 'o';
		public const int DBUS_TYPE_ARRAY = 'a';
		public const int DBUS_TYPE_VARIANT = 'v';
		public const int DBUS_TYPE_STRUCT = 'r';
		public const int DBUS_TYPE_DICT_ENTRY = 'e';
		public const int DBUS_TYPE_INVALID = 0;

		[StructLayout(LayoutKind.Sequential)]
		public struct DBusError
		{
			public IntPtr name;
			public IntPtr message;
			public uint dummy;
			public IntPtr padding1;
		}

		[DllImport(DBUS)]
		public static extern void dbus_error_init(ref DBusError error);

		[DllImport(DBUS)]
		public static extern void dbus_error_free(ref DBusError error);

		[DllImport(DBUS)]
		public static extern uint dbus_error_is_set(ref DBusError error);

		[DllImport(DBUS)]
		public static extern IntPtr dbus_bus_get_private(int type, ref DBusError error);

		[DllImport(DBUS)]
		public static extern IntPtr dbus_bus_get_unique_name(IntPtr connection);

		[DllImport(DBUS)]
		public static extern void dbus_bus_add_match(IntPtr connection, string rule, ref DBusError error);

		[DllImport(DBUS)]
		public static extern void dbus_connection_set_exit_on_disconnect(IntPtr connection, uint exitOnDisconnect);

		[DllImport(DBUS)]
		public static extern void dbus_connection_close(IntPtr connection);

		[DllImport(DBUS)]
		public static extern void dbus_connection_unref(IntPtr connection);

		[DllImport(DBUS)]
		public static extern IntPtr dbus_connection_send_with_reply_and_block(IntPtr connection, IntPtr message, int timeoutMilliseconds, ref DBusError error);

		[DllImport(DBUS)]
		public static extern uint dbus_connection_read_write(IntPtr connection, int timeoutMilliseconds);

		[DllImport(DBUS)]
		public static extern IntPtr dbus_connection_pop_message(IntPtr connection);

		[DllImport(DBUS)]
		public static extern IntPtr dbus_message_new_method_call(string destination, string path, string iface, string method);

		[DllImport(DBUS)]
		public static extern void dbus_message_unref(IntPtr message);

		[DllImport(DBUS)]
		public static extern uint dbus_message_is_signal(IntPtr message, string iface, string signalName);

		[DllImport(DBUS)]
		public static extern IntPtr dbus_message_get_path(IntPtr message);

		[DllImport(DBUS)]
		public static extern void dbus_message_iter_init_append(IntPtr message, IntPtr iter);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_append_basic(IntPtr iter, int type, ref IntPtr value);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_append_basic(IntPtr iter, int type, ref uint value);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_append_fixed_array(IntPtr iter, int elementType, ref IntPtr value, int elements);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_open_container(IntPtr iter, int type, string? containedSignature, IntPtr sub);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_close_container(IntPtr iter, IntPtr sub);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_init(IntPtr message, IntPtr iter);

		[DllImport(DBUS)]
		public static extern int dbus_message_iter_get_arg_type(IntPtr iter);

		/// <remarks>
		/// libdbus writes only as many bytes as the type has (4 for a uint, 8 for a string's
		/// pointer), so <paramref name="value"/> must start at 0 for the rest to read true.
		/// </remarks>
		[DllImport(DBUS)]
		public static extern void dbus_message_iter_get_basic(IntPtr iter, ref ulong value);

		[DllImport(DBUS)]
		public static extern uint dbus_message_iter_next(IntPtr iter);

		[DllImport(DBUS)]
		public static extern void dbus_message_iter_recurse(IntPtr iter, IntPtr sub);
	}
}
