using System.Windows.Forms;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The question asked when a core's machine dies. What it offers depends on what the greenzone
	/// can still produce, and every button reports exactly one choice - the actions themselves live
	/// in MainForm and TAStudio, and are never taken by the window.
	/// </summary>
	[TestClass]
	public class CoreStoppedFormTests
	{
		private static Button ButtonNamed(Form form, string name)
			=> (Button) form.Controls.Find(name, searchAllChildren: true)[0];

		[TestMethod]
		public void ItSaysWhyAndWhereTheSafePointIs()
		{
			using CoreStoppedForm form = new("the core aborted. It said: out of memory", stoppedAt: 1234,
				safePoint: 1200, canRestart: true, canSave: true);
			form.Show();
			Assert.AreEqual("The core stopped", form.Text);
			StringAssert.Contains(form.Controls.Find("Reason", true)[0].Text, "out of memory");
			StringAssert.Contains(ButtonNamed(form, "BackToSafePoint").Text, "frame 1200");
			Assert.IsTrue(ButtonNamed(form, "BackToSafePoint").Enabled);
			Assert.IsTrue(ButtonNamed(form, "RestartFromFrameZero").Enabled);
			Assert.IsTrue(ButtonNamed(form, "SaveInputsAndClose").Enabled);
			Assert.IsTrue(ButtonNamed(form, "CloseWithoutSaving").Enabled);
		}

		/// <summary>A greenzone that holds nothing cannot be gone back to, and a restart needs its frame 0.</summary>
		[TestMethod]
		public void WhatTheGreenzoneCannotProduceIsNotOffered()
		{
			using CoreStoppedForm form = new("the core exited (status 1)", stoppedAt: 50,
				safePoint: -1, canRestart: false, canSave: false);
			form.Show();
			Assert.IsFalse(ButtonNamed(form, "BackToSafePoint").Enabled);
			Assert.IsFalse(ButtonNamed(form, "RestartFromFrameZero").Enabled);
			Assert.IsFalse(ButtonNamed(form, "SaveInputsAndClose").Enabled);
			Assert.IsTrue(ButtonNamed(form, "CloseWithoutSaving").Enabled, "closing is always possible");
		}

		[TestMethod]
		public void EachButtonReportsItsChoice()
		{
			foreach (var (name, choice) in new[]
			{
				("BackToSafePoint", CoreStoppedForm.Choice.BackToSafePoint),
				("RestartFromFrameZero", CoreStoppedForm.Choice.RestartFromFrameZero),
				("SaveInputsAndClose", CoreStoppedForm.Choice.SaveInputsAndClose),
				("CloseWithoutSaving", CoreStoppedForm.Choice.CloseWithoutSaving),
			})
			{
				using CoreStoppedForm form = new("the core crashed", stoppedAt: 10, safePoint: 8, canRestart: true, canSave: true);
				form.Show();
				Assert.AreEqual(CoreStoppedForm.Choice.None, form.Chosen, "nothing is chosen until a button is");
				ButtonNamed(form, name).PerformClick();
				Assert.AreEqual(choice, form.Chosen, name);
			}
		}
	}
}
