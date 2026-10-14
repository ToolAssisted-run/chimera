#nullable enable

using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

using Chimera.Common;

using static Chimera.Common.DBusImports;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The desktop's own file chooser, asked for through the XDG desktop portal
	/// (<c>org.freedesktop.portal.FileChooser</c> on the session D-Bus). It is the dialog
	/// the person's desktop draws for every program - KDE's, GNOME's, or whichever their
	/// portal is set to - with their bookmarks, recent places and typed paths, instead of
	/// Mono's toolkit dialog. See docs/folder-picker.md.
	/// </summary>
	/// <remarks>
	/// Nothing here is required. Where there is no session bus, no portal, or a portal with
	/// no file chooser behind it, <see cref="Show"/> answers <see cref="Outcome.Unavailable"/>
	/// and the caller shows the toolkit dialog it always did.
	/// </remarks>
	internal static class PortalFileChooser
	{
		private const string Service = "org.freedesktop.portal.Desktop";
		private const string ObjectPath = "/org/freedesktop/portal/desktop";
		private const string Interface = "org.freedesktop.portal.FileChooser";
		private const string RequestInterface = "org.freedesktop.portal.Request";

		/// <summary>The choice id the one tick box is sent and read back under.</summary>
		private const string CheckBoxId = "checkbox";

		public enum Outcome { Chosen, Cancelled, Unavailable }

		public sealed class Request
		{
			public bool Save;
			public string Title = "";
			public bool Multiple;
			public bool Directory;
			/// <summary>A WinForms filter string, <c>Label|*.a;*.b|All files|*.*</c>.</summary>
			public string Filter = "";
			/// <summary>1-based, as WinForms counts; 0 or out of range is the first.</summary>
			public int FilterIndex = 1;
			public string Folder = "";
			/// <summary>The name a save dialog opens with.</summary>
			public string FileName = "";
			/// <summary>The label of a tick box to carry in the dialog; null for none.</summary>
			public string? CheckBoxLabel;
			public bool CheckBoxChecked;
		}

		public sealed class Answer
		{
			public IReadOnlyList<string> Paths = [ ];
			/// <summary>1-based; 0 when the portal did not say which filter was in use.</summary>
			public int FilterIndex;
			/// <summary>The tick box as it was confirmed; null when the dialog did not carry it.</summary>
			public bool? CheckBoxChecked;
		}

		private static readonly Lazy<uint> PortalVersion = new(AskVersion, LazyThreadSafetyMode.ExecutionAndPublication);

		/// <summary>Whether the desktop has a file chooser to ask, and so whether files go to it.</summary>
		public static bool Available => PortalVersion.Value >= 1;

		/// <summary>Whether it can pick a folder (version 3 of the interface and later).</summary>
		public static bool CanPickFolders => PortalVersion.Value >= 3;

		/// <summary>
		/// Shows the desktop's chooser and waits for it as a modal dialog would: Chimera's
		/// windows take no clicks, keys or closing, and are still painted, until the person
		/// answers.
		/// </summary>
		public static Outcome Show(IWin32Window? owner, Request request, out Answer answer)
		{
			answer = new();
			if (request.Directory ? !CanPickFolders : !Available) return Outcome.Unavailable;
			var connection = Connect();
			if (connection == IntPtr.Zero) return Outcome.Unavailable;
			// The windows are kept from input and not disabled: a disabled Mono window that is
			// modal itself (Config > Firmware is) kept the focus, and the chooser opened behind
			// it and could not be brought in front (Plasma and GNOME on Wayland, 2026-10-14).
			var blocked = new InputBlocker();
			Application.AddMessageFilter(blocked);
			try
			{
				var asked = Stopwatch.StartNew();
				var code = Ask(connection, request, ParentWindow(owner), answer);
				// 0 is an answer and 1 is Cancel. 2 is "ended some other way", and it is Cancel
				// too: shana answers every Cancel with 2 (2026-10-14). Only a 2 sooner than
				// anyone could press Cancel is a chooser that never came up, and then the
				// toolkit dialog is offered instead.
				return code switch
				{
					null => Outcome.Unavailable,
					0 => answer.Paths.Count is 0 ? Outcome.Cancelled : Outcome.Chosen,
					2 when asked.ElapsedMilliseconds < NeverShownMilliseconds => Outcome.Unavailable,
					_ => Outcome.Cancelled,
				};
			}
			finally
			{
				Application.RemoveMessageFilter(blocked);
				(owner as Form)?.Activate();
				dbus_connection_close(connection);
				dbus_connection_unref(connection);
			}
		}

		private static int _tokens;

		/// <summary>
		/// Throws away what a person does to Chimera's windows while the chooser is open: mouse
		/// buttons and wheel, keys, and the window manager's close. Mouse movement and painting
		/// still go through.
		/// </summary>
		private sealed class InputBlocker : IMessageFilter
		{
			private const int WM_CLOSE = 0x0010;
			private const int WM_KEYFIRST = 0x0100;
			private const int WM_KEYLAST = 0x0109;
			private const int WM_NCLBUTTONDOWN = 0x00A1;
			private const int WM_NCMBUTTONDBLCLK = 0x00A9;
			private const int WM_LBUTTONDOWN = 0x0201;
			private const int WM_MOUSELAST = 0x020E;

			public bool PreFilterMessage(ref Message m)
				=> m.Msg is WM_CLOSE
					or >= WM_KEYFIRST and <= WM_KEYLAST
					or >= WM_NCLBUTTONDOWN and <= WM_NCMBUTTONDBLCLK
					or >= WM_LBUTTONDOWN and <= WM_MOUSELAST;
		}

		/// <summary>How soon a "failed" answer means the chooser was never shown at all.</summary>
		private const long NeverShownMilliseconds = 300;

		/// <summary>
		/// Asks once and waits for the answer, keeping the windows painted. The response code
		/// (0 chosen, 1 cancelled, 2 failed), or null if the portal could not be asked at all.
		/// </summary>
		private static uint? Ask(IntPtr connection, Request request, string parent, Answer answer)
		{
			var token = $"chimera{Environment.TickCount & 0x7FFFFFFF}x{Interlocked.Increment(ref _tokens)}";
			// The answer comes as a signal on a request object whose path is known from the
			// connection's name and the token, so the match goes in BEFORE the call: a portal
			// that answers at once cannot answer to nobody.
			var expected = $"/org/freedesktop/portal/desktop/request/{UniqueNameAsPathPart(connection)}/{token}";
			if (!AddMatch(connection, expected)) return null;

			var path = Call(connection, request, parent, token);
			if (path is null) return null;
			if (path != expected && !AddMatch(connection, path)) return null;

			while (true)
			{
				if (dbus_connection_read_write(connection, 20) == 0) return null; // the bus went
				IntPtr message;
				while ((message = dbus_connection_pop_message(connection)) != IntPtr.Zero)
				{
					try
					{
						if (dbus_message_is_signal(message, RequestInterface, "Response") == 0) continue;
						if (ReadUtf8(dbus_message_get_path(message)) != path) continue;
						return ReadResponse(message, request, answer);
					}
					finally
					{
						dbus_message_unref(message);
					}
				}
				Application.DoEvents();
			}
		}

		/// <summary>
		/// Splits a WinForms filter string into the portal's filters: a label, and the glob
		/// patterns that go with it. WinForms' <c>*.*</c> means every file; as a glob it would
		/// miss a file with no dot in its name, so it goes as <c>*</c>.
		/// </summary>
		internal static List<(string Label, List<string> Globs)> FiltersFrom(string filter)
		{
			var parts = filter.Split('|');
			var filters = new List<(string, List<string>)>();
			for (var i = 0; i + 1 < parts.Length; i += 2)
			{
				var globs = parts[i + 1].Split(';')
					.Select(static g => g.Trim())
					.Where(static g => g.Length is not 0)
					.Select(static g => g is "*.*" ? "*" : g)
					.ToList();
				if (globs.Count is 0) continue;
				filters.Add((parts[i].Trim(), globs));
			}
			return filters;
		}

		/// <summary>A <c>file://</c> URI the portal answers with, as a local path; null for any other kind.</summary>
		internal static string? PathFromUri(string uri)
			=> Uri.TryCreate(uri, UriKind.Absolute, out var parsed) && parsed.IsFile ? parsed.LocalPath : null;

		/// <summary>
		/// The portal's name for a request object: the connection's unique name without its
		/// colon and with its dots as underscores (<c>:1.42</c> is <c>1_42</c>).
		/// </summary>
		internal static string UniqueNameAsPathPart(string uniqueName) => uniqueName.TrimStart(':').Replace('.', '_');

		private static string UniqueNameAsPathPart(IntPtr connection) => UniqueNameAsPathPart(ReadUtf8(dbus_bus_get_unique_name(connection)) ?? "");

		private static uint AskVersion()
		{
			if (!OSTailoredCode.IsUnixHost || OSTailoredCode.CurrentOS is OSTailoredCode.DistinctOS.macOS) return 0;
			try
			{
				var connection = Connect();
				if (connection == IntPtr.Zero) return 0;
				try
				{
					using var writer = new Writer("org.freedesktop.DBus.Properties", "Get");
					writer.String(Interface);
					writer.String("version");
					var reply = writer.Send(connection, 5000);
					if (reply == IntPtr.Zero) return 0;
					try
					{
						using var iter = new Iter();
						if (dbus_message_iter_init(reply, iter) == 0 || dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT) return 0;
						using var inner = new Iter();
						dbus_message_iter_recurse(iter, inner);
						if (dbus_message_iter_get_arg_type(inner) != DBUS_TYPE_UINT32) return 0;
						ulong version = 0;
						dbus_message_iter_get_basic(inner, ref version);
						return (uint) version;
					}
					finally
					{
						dbus_message_unref(reply);
					}
				}
				finally
				{
					dbus_connection_close(connection);
					dbus_connection_unref(connection);
				}
			}
			catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException)
			{
				return 0; // no libdbus: the toolkit dialog, as before
			}
		}

		/// <summary>
		/// A connection of this picker's own to the session bus, or zero. Private, so closing
		/// it touches nothing else; and told not to end the process if the bus goes away,
		/// which is libdbus' default.
		/// </summary>
		private static IntPtr Connect()
		{
			// With no address libdbus would try to launch a bus of its own; a desktop with a
			// portal has one, at the address or at the runtime directory's bus.
			if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("DBUS_SESSION_BUS_ADDRESS"))
				&& !File.Exists(Path.Combine(Environment.GetEnvironmentVariable("XDG_RUNTIME_DIR") ?? "/nonexistent", "bus")))
			{
				return IntPtr.Zero;
			}
			var error = NewError();
			var connection = dbus_bus_get_private(DBUS_BUS_SESSION, ref error);
			var failed = dbus_error_is_set(ref error) != 0;
			dbus_error_free(ref error);
			if (failed || connection == IntPtr.Zero) return IntPtr.Zero;
			dbus_connection_set_exit_on_disconnect(connection, 0);
			return connection;
		}

		private static bool AddMatch(IntPtr connection, string path)
		{
			var error = NewError();
			dbus_bus_add_match(connection, $"type='signal',interface='{RequestInterface}',member='Response',path='{path}'", ref error);
			var failed = dbus_error_is_set(ref error) != 0;
			dbus_error_free(ref error);
			return !failed;
		}

		private static DBusError NewError()
		{
			DBusError error = default;
			dbus_error_init(ref error);
			return error;
		}

		/// <summary>Sends OpenFile or SaveFile; the request object's path, or null if the portal refused.</summary>
		private static string? Call(IntPtr connection, Request request, string parent, string token)
		{
			using var writer = new Writer(Interface, request.Save ? "SaveFile" : "OpenFile");
			writer.String(parent);
			writer.String(request.Title.Length is 0 ? (request.Save ? "Save" : request.Directory ? "Select Folder" : "Open") : request.Title);
			writer.OpenDict();
			writer.Entry("handle_token", "s", w => w.String(token));
			writer.Entry("modal", "b", w => w.Boolean(true));
			if (!request.Save)
			{
				writer.Entry("multiple", "b", w => w.Boolean(request.Multiple));
				if (request.Directory) writer.Entry("directory", "b", w => w.Boolean(true));
			}
			var filters = request.Directory ? new List<(string Label, List<string> Globs)>() : FiltersFrom(request.Filter);
			if (filters.Count is not 0)
			{
				writer.Entry("filters", "a(sa(us))", w =>
				{
					w.OpenArray("(sa(us))");
					foreach (var filter in filters) w.FilterTuple(filter.Label, filter.Globs);
					w.Close();
				});
				var current = filters[request.FilterIndex >= 1 && request.FilterIndex <= filters.Count ? request.FilterIndex - 1 : 0];
				writer.Entry("current_filter", "(sa(us))", w => w.FilterTuple(current.Label, current.Globs));
			}
			if (request.CheckBoxLabel is not null)
			{
				// a choice with no options is a tick box; its value is "true" or "false"
				writer.Entry("choices", "a(ssa(ss)s)", w =>
				{
					w.OpenArray("(ssa(ss)s)");
					w.OpenStruct();
					w.String(CheckBoxId);
					w.String(request.CheckBoxLabel);
					w.OpenArray("(ss)");
					w.Close();
					w.String(request.CheckBoxChecked ? "true" : "false");
					w.Close();
					w.Close();
				});
			}
			if (request.Save && request.FileName.Length is not 0)
			{
				writer.Entry("current_name", "s", w => w.String(Path.GetFileName(request.FileName)));
			}
			if (request.Folder.Length is not 0 && System.IO.Directory.Exists(request.Folder))
			{
				writer.Entry("current_folder", "ay", w => w.NulTerminatedBytes(request.Folder));
			}
			writer.Close();

			var reply = writer.Send(connection, 5000);
			if (reply == IntPtr.Zero) return null;
			try
			{
				using var iter = new Iter();
				if (dbus_message_iter_init(reply, iter) == 0 || dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_OBJECT_PATH) return null;
				ulong value = 0;
				dbus_message_iter_get_basic(iter, ref value);
				return ReadUtf8((IntPtr) (long) value);
			}
			finally
			{
				dbus_message_unref(reply);
			}
		}

		/// <summary>Reads a Response signal, <c>(u response, a{sv} results)</c>, into the answer; returns the response code.</summary>
		private static uint ReadResponse(IntPtr message, Request request, Answer answer)
		{
			using var iter = new Iter();
			if (dbus_message_iter_init(message, iter) == 0 || dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_UINT32) return 2;
			ulong code = 0;
			dbus_message_iter_get_basic(iter, ref code);
			if (code is not 0 || dbus_message_iter_next(iter) == 0 || dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) return (uint) code;

			var filters = FiltersFrom(request.Filter);
			using var entries = new Iter();
			using var entry = new Iter();
			using var value = new Iter();
			dbus_message_iter_recurse(iter, entries);
			while (dbus_message_iter_get_arg_type(entries) == DBUS_TYPE_DICT_ENTRY)
			{
				dbus_message_iter_recurse(entries, entry);
				var key = GetString(entry);
				dbus_message_iter_next(entry);
				dbus_message_iter_recurse(entry, value); // into the variant
				switch (key)
				{
					case "uris":
						answer.Paths = Strings(value).Select(PathFromUri).OfType<string>().ToArray();
						break;
					case "current_filter" when dbus_message_iter_get_arg_type(value) == DBUS_TYPE_STRUCT:
						using (var tuple = new Iter())
						{
							dbus_message_iter_recurse(value, tuple);
							var label = GetString(tuple);
							var at = filters.FindIndex(f => f.Label == label);
							if (at >= 0) answer.FilterIndex = at + 1;
						}
						break;
					case "choices" when dbus_message_iter_get_arg_type(value) == DBUS_TYPE_ARRAY:
						using (var pairs = new Iter())
						using (var pair = new Iter())
						{
							dbus_message_iter_recurse(value, pairs);
							while (dbus_message_iter_get_arg_type(pairs) == DBUS_TYPE_STRUCT)
							{
								dbus_message_iter_recurse(pairs, pair);
								var id = GetString(pair);
								dbus_message_iter_next(pair);
								if (id is CheckBoxId) answer.CheckBoxChecked = GetString(pair) is "true";
								dbus_message_iter_next(pairs);
							}
						}
						break;
				}
				dbus_message_iter_next(entries);
			}
			return 0;
		}

		private static IEnumerable<string> Strings(Iter array)
		{
			var list = new List<string>();
			if (dbus_message_iter_get_arg_type(array) != DBUS_TYPE_ARRAY) return list;
			using var item = new Iter();
			dbus_message_iter_recurse(array, item);
			while (dbus_message_iter_get_arg_type(item) == DBUS_TYPE_STRING)
			{
				if (GetString(item) is { } s) list.Add(s);
				dbus_message_iter_next(item);
			}
			return list;
		}

		private static string? GetString(Iter iter)
		{
			var type = dbus_message_iter_get_arg_type(iter);
			if (type != DBUS_TYPE_STRING && type != DBUS_TYPE_OBJECT_PATH) return null;
			ulong value = 0;
			dbus_message_iter_get_basic(iter, ref value);
			return ReadUtf8((IntPtr) (long) value);
		}

		private static string? ReadUtf8(IntPtr p)
		{
			if (p == IntPtr.Zero) return null;
			var length = 0;
			while (Marshal.ReadByte(p, length) is not 0) length++;
			var bytes = new byte[length];
			Marshal.Copy(p, bytes, 0, length);
			return Encoding.UTF8.GetString(bytes);
		}

		/// <summary>
		/// The window the chooser belongs to, as the portal names one: <c>x11:</c> and the X
		/// window in hex. Mono's handle is its client area, so the frame is asked of Mono's
		/// own bookkeeping; anything that fails says "no parent", which the portal allows.
		/// </summary>
		/// <remarks>
		/// In a Wayland session Chimera's windows are XWayland's, and a chooser drawn on
		/// Wayland is given no parent. An X11 parent there crashed GNOME's chooser (Nautilus,
		/// under niri, on any X11 window it was given), and attached no dialog anywhere it was
		/// tried; the dialog is modal regardless, since Chimera's windows take no input meanwhile.
		/// </remarks>
		private static string ParentWindow(IWin32Window? owner)
		{
			if (owner is null || Environment.GetEnvironmentVariable("DISPLAY") is null or "") return "";
			if (Environment.GetEnvironmentVariable("WAYLAND_DISPLAY") is { Length: not 0 }) return "";
			try
			{
				var hwndType = typeof(Control).Assembly.GetType("System.Windows.Forms.Hwnd");
				var hwnd = hwndType?.GetMethod("ObjectFromHandle", BindingFlags.Public | BindingFlags.Static)?.Invoke(null, [ owner.Handle ]);
				var frame = hwndType?.GetProperty("WholeWindow")?.GetValue(hwnd);
				return frame is IntPtr x && x != IntPtr.Zero ? $"x11:{x.ToInt64():x}" : "";
			}
			catch (Exception)
			{
				return "";
			}
		}

		/// <summary>A <c>DBusMessageIter</c> in memory that does not move.</summary>
		private sealed class Iter : IDisposable
		{
			private IntPtr _memory = Marshal.AllocHGlobal(IterSize);

			public static implicit operator IntPtr(Iter iter) => iter._memory;

			public void Dispose()
			{
				Marshal.FreeHGlobal(_memory);
				_memory = IntPtr.Zero;
			}
		}

		/// <summary>
		/// Builds one method call to the portal. Containers are opened and closed in pairs,
		/// as libdbus requires; every string and iterator is freed with the writer.
		/// </summary>
		private sealed class Writer : IDisposable
		{
			private readonly IntPtr _message;
			private readonly Stack<Iter> _open = new();
			private readonly List<IntPtr> _strings = [ ];
			private readonly List<Iter> _iters = [ ];

			public Writer(string iface, string method)
			{
				_message = dbus_message_new_method_call(Service, ObjectPath, iface, method);
				var root = new Iter();
				_iters.Add(root);
				dbus_message_iter_init_append(_message, root);
				_open.Push(root);
			}

			private IntPtr Top => _open.Peek();

			public void String(string s)
			{
				var bytes = Encoding.UTF8.GetBytes(s);
				var p = Marshal.AllocHGlobal(bytes.Length + 1);
				_strings.Add(p);
				Marshal.Copy(bytes, 0, p, bytes.Length);
				Marshal.WriteByte(p, bytes.Length, 0);
				dbus_message_iter_append_basic(Top, DBUS_TYPE_STRING, ref p);
			}

			public void Boolean(bool b)
			{
				var value = b ? 1u : 0u;
				dbus_message_iter_append_basic(Top, DBUS_TYPE_BOOLEAN, ref value);
			}

			public void Uint(uint u) => dbus_message_iter_append_basic(Top, DBUS_TYPE_UINT32, ref u);

			/// <summary>A path as the portal takes a folder: its bytes, and a NUL.</summary>
			public void NulTerminatedBytes(string s)
			{
				var bytes = Encoding.UTF8.GetBytes(s);
				var p = Marshal.AllocHGlobal(bytes.Length + 1);
				_strings.Add(p);
				Marshal.Copy(bytes, 0, p, bytes.Length);
				Marshal.WriteByte(p, bytes.Length, 0);
				Open(DBUS_TYPE_ARRAY, "y");
				dbus_message_iter_append_fixed_array(Top, DBUS_TYPE_BYTE, ref p, bytes.Length + 1);
				Close();
			}

			public void OpenArray(string elementSignature) => Open(DBUS_TYPE_ARRAY, elementSignature);

			public void OpenStruct() => Open(DBUS_TYPE_STRUCT, null);

			public void OpenDict() => Open(DBUS_TYPE_ARRAY, "{sv}");

			/// <summary>One <c>{sv}</c> of the options: a key and a value of the signature given.</summary>
			public void Entry(string key, string signature, Action<Writer> value)
			{
				Open(DBUS_TYPE_DICT_ENTRY, null);
				String(key);
				Open(DBUS_TYPE_VARIANT, signature);
				value(this);
				Close();
				Close();
			}

			/// <summary>A filter, <c>(sa(us))</c>: a label and its patterns, each a glob (type 0).</summary>
			public void FilterTuple(string label, IEnumerable<string> globs)
			{
				OpenStruct();
				String(label);
				OpenArray("(us)");
				foreach (var glob in globs)
				{
					OpenStruct();
					Uint(0);
					String(glob);
					Close();
				}
				Close();
				Close();
			}

			private void Open(int type, string? signature)
			{
				var sub = new Iter();
				_iters.Add(sub);
				dbus_message_iter_open_container(Top, type, signature, sub);
				_open.Push(sub);
			}

			public void Close()
			{
				var sub = _open.Pop();
				dbus_message_iter_close_container(Top, sub);
			}

			/// <summary>Sends the call and waits for its reply; zero if it failed.</summary>
			public IntPtr Send(IntPtr connection, int timeoutMilliseconds)
			{
				var error = NewError();
				var reply = dbus_connection_send_with_reply_and_block(connection, _message, timeoutMilliseconds, ref error);
				var failed = dbus_error_is_set(ref error) != 0;
				dbus_error_free(ref error);
				if (!failed) return reply;
				if (reply != IntPtr.Zero) dbus_message_unref(reply);
				return IntPtr.Zero;
			}

			public void Dispose()
			{
				dbus_message_unref(_message);
				foreach (var iter in _iters) iter.Dispose();
				foreach (var p in _strings) Marshal.FreeHGlobal(p);
			}
		}
	}
}
