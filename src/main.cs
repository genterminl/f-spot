using Gtk;
using Gnome;
using System;
using System.IO;

public class Driver {

	public static void Main (string [] args)
	{
		Application.Init ();
		Program program = new Program ("F-Spot", "0.0", Modules.UI, args);

		StockIcons.Initialize ();

		// FIXME: Error checking is non-existant here...

		string home_directory = Environment.GetEnvironmentVariable ("HOME");
		Directory.SetCurrentDirectory (home_directory);

		string base_directory = home_directory + "/.gnome2/f-spot";
		if (! File.Exists (base_directory))
			Directory.CreateDirectory (base_directory);

		Db db = new Db (base_directory + "/photos.db", true);
		
		Gtk.Window.DefaultIconList = new Gdk.Pixbuf [] {PixbufUtils.LoadFromAssembly ("f-spot-camera.png")};

		MainWindow main_window = new MainWindow (db);
		
		ParseCommands (args);

		program.Run ();
	}

	private static void  ParseCommands (string [] args) 
	{
		for (int i = 0; i < args.Length; i++) {
			if (args [i] == "--import") {
				if (++i < args.Length && (File.Exists (args [i]) || Directory.Exists (args[i]))) {
					UriList urilist = new UriList (new string [] {args [i]});
					MainWindow.Toplevel.ImportUriList (urilist);
				} else {
					System.Console.WriteLine ("no valid path to import from");
				}
			}
		}
	}
}
