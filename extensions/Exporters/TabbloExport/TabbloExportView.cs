//
// FSpotTabbloExport.TabbloExportView
//
// Authors:
//	Wojciech Dzierzanowski (wojciech.dzierzanowski@gmail.com)
//
// (C) Copyright 2009 Wojciech Dzierzanowski
//

// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

using System;
using System.Diagnostics;


namespace FSpotTabbloExport {


	class TabbloExportView {

		private const string DialogName = "tabblo_export_dialog";

		[Glade.Widget] internal Gtk.Dialog tabblo_export_dialog;

		[Glade.Widget] private Gtk.ScrolledWindow thumb_scrolled_window;

		[Glade.Widget] internal Gtk.Entry username_entry;
		[Glade.Widget] internal Gtk.Entry password_entry;

		[Glade.Widget] internal Gtk.CheckButton attach_tags_button;
		[Glade.Widget] private Gtk.Alignment attached_tags_alignment;
		internal FSpot.Widgets.TagView attached_tags_view; 
		[Glade.Widget] internal Gtk.Button attached_tags_select_button;

		[Glade.Widget] internal Gtk.CheckButton remove_tags_button;
		[Glade.Widget] private Gtk.Alignment removed_tags_alignment;
		internal FSpot.Widgets.TagView removed_tags_view; 
		[Glade.Widget] internal Gtk.Button removed_tags_select_button;

		[Glade.Widget] private Gtk.Button export_button;


		// The dialog is constructed and shown.
		internal TabbloExportView (FSpot.IBrowsableCollection photos)
		{
			Glade.XML glade_xml = new Glade.XML (
					null, "TabbloExport.glade", DialogName,
					"f-spot");
			glade_xml.Autoconnect (this);

			// Thumbnails
			FSpot.Widgets.IconView icon_view =
					new FSpot.Widgets.IconView (photos);
			icon_view.DisplayDates = false;
			icon_view.DisplayTags = false;

			thumb_scrolled_window.Add (icon_view);
			icon_view.Show ();

			// Tags
			attached_tags_view = new FSpot.Widgets.TagView ();
			attached_tags_alignment.Add (attached_tags_view);
			attached_tags_view.Show ();

			removed_tags_view = new FSpot.Widgets.TagView ();
			removed_tags_alignment.Add (removed_tags_view);
			removed_tags_view.Show ();

			tabblo_export_dialog.Show ();
		}


		internal void Destroy ()
		{
			Debug.Assert (null != tabblo_export_dialog);
			tabblo_export_dialog.Destroy ();
		}


		internal void ResetFocus ()
		{
			export_button.HasFocus = true;
		}


		internal bool Validated {
			set {
				export_button.Sensitive = value;
			}
		}
	}
}
