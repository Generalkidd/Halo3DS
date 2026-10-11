using HaloSetup.Cli;

using var cancellation = new CancellationTokenSource();
Console.CancelKeyPress += (_, e) => { e.Cancel = true; cancellation.Cancel(); };
// Double-click launchers request guided mode explicitly; redirected scripts
// without a command must not unexpectedly stop and ask questions.
TextReader? input = !Console.IsInputRedirected || args.SequenceEqual(new[] { "guided" }) ? Console.In : null;
return CommandLine.Run(args, Console.Out, Console.Error, cancellation.Token, input);
