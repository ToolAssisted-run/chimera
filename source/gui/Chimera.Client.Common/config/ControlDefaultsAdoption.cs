#nullable enable

using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace Chimera.Client.Common
{
	/// <summary>
	/// How a core package's default bindings (default_keybinds.json) reach the
	/// config, controller by controller.
	///
	/// A controller the config has never bound takes the package's defaults, as
	/// it always did. What that left out was the NEXT package: defaults adopted
	/// once were the config's forever, so a core that fixed its defaults (the
	/// Triforce panel's keyboard stick, issue #157) never reached anyone who had
	/// already played it, and they saw the very bug the fix was for.
	///
	/// So the config remembers what it adopted (<see cref="Config.AdoptedControlDefaults"/>,
	/// a fingerprint per controller). When the package's defaults change and the
	/// config's bindings are still exactly what it adopted - nobody has touched
	/// them - the new defaults replace them. Bindings a person changed are theirs,
	/// and stay. A config from before this record cannot say which it is, so it
	/// is left alone until its bindings match the package's again (Load Defaults
	/// in the controller dialog), from when on it follows.
	/// </summary>
	public static class ControlDefaultsAdoption
	{
		public static void Apply(Config config, DefaultControls package, string controller)
		{
			var fresh = Fingerprint(package.AllTrollers, package.AllTrollersAutoFire, package.AllTrollersAnalog, package.AllTrollersFeedbacks, controller);
			if (fresh is null) return; // the package says nothing about this controller

			// the package's defaults changed since this config took them, and the
			// config still holds exactly what it took: the new ones replace them
			if (config.AdoptedControlDefaults.TryGetValue(controller, out var adopted)
				&& adopted != fresh
				&& FingerprintOf(config, controller) == adopted)
			{
				if (package.AllTrollers.TryGetValue(controller, out var b)) config.AllTrollers[controller] = new(b);
				if (package.AllTrollersAutoFire.TryGetValue(controller, out var af)) config.AllTrollersAutoFire[controller] = new(af);
				if (package.AllTrollersAnalog.TryGetValue(controller, out var a)) config.AllTrollersAnalog[controller] = new(a);
				if (package.AllTrollersFeedbacks.TryGetValue(controller, out var f)) config.AllTrollersFeedbacks[controller] = new(f);
			}

			// A controller the config names but binds NOTHING for counts as never
			// seen: a session from before the package carried bindings leaves that
			// empty section behind, and it must not shadow the defaults forever.
			AdoptIfEmpty(config.AllTrollers, package.AllTrollers, controller);
			AdoptIfEmpty(config.AllTrollersAutoFire, package.AllTrollersAutoFire, controller);
			AdoptIfEmpty(config.AllTrollersAnalog, package.AllTrollersAnalog, controller);
			AdoptIfEmpty(config.AllTrollersFeedbacks, package.AllTrollersFeedbacks, controller);

			// what the config now matches is what it has adopted
			if (FingerprintOf(config, controller) == fresh) config.AdoptedControlDefaults[controller] = fresh;
		}

		private static void AdoptIfEmpty<T>(Dictionary<string, Dictionary<string, T>> into, Dictionary<string, Dictionary<string, T>> from, string controller)
		{
			if (into.TryGetValue(controller, out var seen) && seen.Count is not 0) return;
			if (from.TryGetValue(controller, out var defaults)) into[controller] = new(defaults);
		}

		private static string? FingerprintOf(Config config, string controller)
			=> Fingerprint(config.AllTrollers, config.AllTrollersAutoFire, config.AllTrollersAnalog, config.AllTrollersFeedbacks, controller, always: true);

		/// <summary>
		/// One controller's bindings as one string. An empty binding is the same as
		/// none (the controller dialog writes a row for every control whether it is
		/// bound or not, so opening and saving it changes nothing here). Null when
		/// there is nothing to fingerprint and <paramref name="always"/> is false.
		/// </summary>
		internal static string? Fingerprint(
			Dictionary<string, Dictionary<string, string>> buttons,
			Dictionary<string, Dictionary<string, string>> autoFire,
			Dictionary<string, Dictionary<string, AnalogBind>> analog,
			Dictionary<string, Dictionary<string, FeedbackBind>> feedbacks,
			string controller,
			bool always = false)
		{
			var hasAny = buttons.ContainsKey(controller) || autoFire.ContainsKey(controller)
				|| analog.ContainsKey(controller) || feedbacks.ContainsKey(controller);
			if (!hasAny && !always) return null;

			StringBuilder text = new();
			void Section(string tag, IEnumerable<KeyValuePair<string, string>> rows)
			{
				text.Append(tag).Append('\n');
				foreach (var (name, bind) in rows.Where(static r => r.Value.Length is not 0).OrderBy(static r => r.Key, System.StringComparer.Ordinal))
				{
					text.Append(name).Append('=').Append(bind).Append('\n');
				}
			}
			Section("buttons", buttons.TryGetValue(controller, out var b) ? b.Select(static r => new KeyValuePair<string, string>(r.Key, r.Value ?? "")) : [ ]);
			Section("autofire", autoFire.TryGetValue(controller, out var af) ? af.Select(static r => new KeyValuePair<string, string>(r.Key, r.Value ?? "")) : [ ]);
			Section("analog", analog.TryGetValue(controller, out var a)
				? a.Select(static r => new KeyValuePair<string, string>(r.Key,
					string.IsNullOrEmpty(r.Value.Value) && string.IsNullOrEmpty(r.Value.ButtonBindPositive) && string.IsNullOrEmpty(r.Value.ButtonBindNegative)
						? ""
						: string.Join("|", r.Value.Value ?? "", r.Value.Mult.ToString("R", CultureInfo.InvariantCulture),
							r.Value.Deadzone.ToString("R", CultureInfo.InvariantCulture), r.Value.ButtonBindPositive ?? "", r.Value.ButtonBindNegative ?? "")))
				: [ ]);
			Section("feedbacks", feedbacks.TryGetValue(controller, out var f)
				? f.Select(static r => new KeyValuePair<string, string>(r.Key, r.Value.IsZeroed ? "" : $"{r.Value.GamepadPrefix}|{r.Value.Channels}"))
				: [ ]);

			using var sha1 = SHA1.Create();
			return string.Concat(sha1.ComputeHash(Encoding.UTF8.GetBytes(text.ToString())).Select(static x => x.ToString("x2")));
		}
	}
}
