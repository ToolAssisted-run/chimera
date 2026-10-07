// These tests build real WinForms windows and drive them, so they share one UI
// thread and one message loop: nothing here may run beside anything else. That
// is what MSTest does by default, and this says so out loud (MSTEST0001) rather
// than leaving it to the default. The other test assemblies, which touch no
// windows, declare Parallelize instead.
[assembly: DoNotParallelize]
