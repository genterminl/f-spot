using Hyena;

using System;
using System.IO;
using System.Collections;

using FSpot.Utils;
using Mono.Unix;
using Mono.Unix.Native;
using Gdk;

using TagLib.Image;

using GFileInfo = GLib.FileInfo;

namespace FSpot.Imaging {
	public class ImageFormatException : ApplicationException {
		public ImageFormatException (string msg) : base (msg)
		{
		}
	}

	public class ImageFile : IDisposable {

#region Factory functionality

		static Hashtable name_table;
		internal static Hashtable NameTable { get { return name_table; } }

		static ImageFile ()
		{
			name_table = new Hashtable ();
			name_table [".svg"] = typeof (FSpot.Imaging.Svg.SvgFile);
			name_table [".gif"] = typeof (ImageFile);
			name_table [".bmp"] = typeof (ImageFile);
			name_table [".pcx"] = typeof (ImageFile);
			name_table [".jpeg"] = typeof (TagLibFile);
			name_table [".jpg"] = typeof (TagLibFile);
			name_table [".png"] = typeof (TagLibFile);
			name_table [".cr2"] = typeof (FSpot.Imaging.Tiff.Cr2File);
			name_table [".nef"] = typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".pef"] = typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".raw"] = typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".kdc"] = typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".arw"] = typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".rw2"] = typeof (FSpot.Imaging.DCRawFile);
			name_table [".tiff"] = typeof (FSpot.Imaging.Tiff.TiffFile);
			name_table [".tif"] = typeof (FSpot.Imaging.Tiff.TiffFile);
			name_table [".orf"] =  typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".srf"] = typeof (FSpot.Imaging.Tiff.NefFile);
			name_table [".dng"] = typeof (FSpot.Imaging.Tiff.DngFile);
			name_table [".crw"] = typeof (FSpot.Imaging.Ciff.CiffFile);
			name_table [".ppm"] = typeof (FSpot.Imaging.Pnm.PnmFile);
			name_table [".mrw"] = typeof (FSpot.Imaging.Mrw.MrwFile);
			name_table [".raf"] = typeof (FSpot.Imaging.Raf.RafFile);
			name_table [".x3f"] = typeof (FSpot.Imaging.X3f.X3fFile);

			// add mimetypes for fallback
			name_table ["image/bmp"]     = name_table ["image/x-bmp"] = name_table [".bmp"];
			name_table ["image/gif"]     = name_table [".gif"];
			name_table ["image/pjpeg"]   = name_table ["image/jpeg"] = name_table ["image/jpg"] = name_table [".jpg"];
			name_table ["image/x-png"]   = name_table ["image/png"]  = name_table [".png"];
			name_table ["image/svg+xml"] = name_table [".svg"];
			name_table ["image/tiff"]    = name_table [".tiff"];
			name_table ["image/x-dcraw"] = name_table [".raw"];
			name_table ["image/x-ciff"]  = name_table [".crw"];
			name_table ["image/x-mrw"]   = name_table [".mrw"];
			name_table ["image/x-x3f"]   = name_table [".x3f"];
			name_table ["image/x-orf"]   = name_table [".orf"];
			name_table ["image/x-nef"]   = name_table [".nef"];
			name_table ["image/x-cr2"]   = name_table [".cr2"];
			name_table ["image/x-raf"]   = name_table [".raf"];

			//as xcf pixbufloader is not part of gdk-pixbuf, check if it's there,
			//and enable it if needed.
			foreach (Gdk.PixbufFormat format in Gdk.Pixbuf.Formats)
				if (format.Name == "xcf") {
					if (format.IsDisabled)
						format.SetDisabled (false);
					name_table [".xcf"] = typeof (ImageFile);
				}
		}

		public static bool HasLoader (SafeUri uri)
		{
			return GetLoaderType (uri) != null;
		}

		static Type GetLoaderType (SafeUri uri)
		{
			string extension = uri.GetExtension ().ToLower ();
			if (extension == ".thm") {
				// Ignore video thumbnails.
				return null;
			}

			Type t = (Type) name_table [extension];

			if (t == null) {
				// check if GIO can find the file, which is not the case
				// with filenames with invalid encoding
				GLib.File f = GLib.FileFactory.NewForUri (uri);
				if (f.QueryExists (null)) {
					GLib.FileInfo info = f.QueryInfo ("standard::type,standard::content-type", GLib.FileQueryInfoFlags.None, null);
					t = (Type) name_table [info.ContentType];
				}
			}

			return t;
		}
		
		public static ImageFile Create (SafeUri uri)
		{
			System.Type t = GetLoaderType (uri);
			ImageFile img;

			if (t != null)
				img = (ImageFile) System.Activator.CreateInstance (t, new object[] { uri });
			else 
				img = new ImageFile (uri);

			return img;
		}

		public static bool IsRaw (SafeUri uri)
		{
			string [] raw_extensions = {
				".arw",
				".crw",
				".cr2",
				".dng",
				".mrw",
				".nef",
				".orf", 
				".pef", 
				".raw",
				".raf",
				".rw2",
			};
			var extension = uri.GetExtension ().ToLower ();
			foreach (string ext in raw_extensions)
				if (ext == extension)
					return true;
			return false;
		}

		public static bool IsJpeg (SafeUri uri)
		{
			string [] jpg_extensions = {".jpg", ".jpeg"};
			var extension = uri.GetExtension ().ToLower ();
			foreach (string ext in jpg_extensions)
				if (ext == extension)
					return true;
			return false;
		}

#endregion

		protected SafeUri uri;

		public ImageFile (SafeUri uri)
		{
			this.uri = uri;
		}

		~ImageFile ()
		{
			Dispose ();
		}

		protected Stream Open ()
		{
			Log.DebugFormat ("open uri = {0}", uri.ToString ());
			return new GLib.GioStream (GLib.FileFactory.NewForUri (uri).Read (null));
		}

		public virtual Stream PixbufStream ()
		{
			return Open ();
		}
		public SafeUri Uri {
			get { return this.uri; }
		}

		public ImageOrientation Orientation {
			get { return GetOrientation (); }
		}

		protected Gdk.Pixbuf TransformAndDispose (Gdk.Pixbuf orig)
		{
			if (orig == null)
				return null;

			Gdk.Pixbuf rotated = FSpot.Utils.PixbufUtils.TransformOrientation (orig, this.Orientation);

			orig.Dispose ();

			return rotated;
		}

		public virtual Gdk.Pixbuf Load ()
		{
			using (Stream stream = PixbufStream ()) {
				Gdk.Pixbuf orig = new Gdk.Pixbuf (stream);
				return TransformAndDispose (orig);
			}
		}

		public virtual Gdk.Pixbuf Load (int max_width, int max_height)
		{
			System.IO.Stream stream = PixbufStream ();
			if (stream == null) {
				Gdk.Pixbuf orig = this.Load ();
				Gdk.Pixbuf scaled = PixbufUtils.ScaleToMaxSize (orig,  max_width, max_height, false);
				orig.Dispose ();
				return scaled;
			}

			using (stream) {
				PixbufUtils.AspectLoader aspect = new PixbufUtils.AspectLoader (max_width, max_height);
				return aspect.Load (stream, Orientation);
			}
		}

		public virtual ImageOrientation GetOrientation ()
		{
			return ImageOrientation.TopLeft;
		}

		// FIXME this need to have an intent just like the loading stuff.
		public virtual Cms.Profile GetProfile ()
		{
			return null;
		}

		public void Dispose ()
		{
			Close ();
			System.GC.SuppressFinalize (this);
		}

		protected virtual void Close ()
		{
		}
	} 

    public class TagLibFile : ImageFile {
        private TagLib.Image.File metadata_file;

        public TagLibFile (SafeUri uri) : base (uri)
        {
            metadata_file = Metadata.Parse (uri);
        }

        ~TagLibFile () {
            metadata_file.Dispose ();
        }

        public override Cms.Profile GetProfile ()
        {
            return null;
        }

        public override ImageOrientation GetOrientation ()
        {
            return metadata_file.ImageTag.Orientation;
        }
    }
}
