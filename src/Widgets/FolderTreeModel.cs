/*
 * FSpot.Widgets.FolderTreeModel.cs
 *
 * Author(s)
 * 	Mike Gemuende <mike@gemuende.de>
 *
 * This is free software. See COPYING for details.
 */

using System;
using System.Collections.Generic;

using Gtk;
using GLib;

using FSpot;
using FSpot.Utils;

using Banshee.Database;

using Mono.Unix;
using Mono.Data.SqliteClient;

namespace FSpot.Widgets
{
	public class FolderTreeModel : TreeStore
	{
		Db database;
		
		const string query_string = 
			"SELECT base_uri, COUNT(*) AS count " +
			"FROM photos " + 
			"GROUP BY base_uri " +
			"ORDER BY base_uri DESC";
		
		
		public FolderTreeModel ()
			: base (typeof (string), typeof (int), typeof (Uri))
		{
			database = MainWindow.Toplevel.Database;
			database.Photos.ItemsChanged += HandlePhotoItemsChanged;
			
			UpdateFolderTree ();
		}
		
		void HandlePhotoItemsChanged (object sender, DbItemEventArgs<Photo> e)
		{
			UpdateFolderTree ();
		}
		
		public string GetFolderNameByIter (TreeIter iter)
		{
			if ( ! IterIsValid (iter))
				return null;
			
			return (string) GetValue (iter, 0);
		}
		
		public int GetPhotoCountByIter (TreeIter iter)
		{
			if ( ! IterIsValid (iter))
				return -1;
			
			return (int) GetValue (iter, 1);
		}
		
		public Uri GetUriByIter (TreeIter iter)
		{
			if ( ! IterIsValid (iter))
				return null;
			
			return (Uri) GetValue (iter, 2);
		}	
		
		public Uri GetUriByPath (TreePath row)
		{
			TreeIter iter;
			
			GetIter (out iter, row);
			
			return GetUriByIter (iter);
		}
		
		int count_all;
		public int Count {
			get { return count_all; }
		}
		
		/*
		 * UpdateFolderTree queries for directories in database and updates
		 * a possibly existing folder-tree to the queried structure
		 */
		private void UpdateFolderTree ()
		{
			Clear ();
			
			count_all = 0;
			
			/* points at start of each iteration to the leaf of the last inserted uri */
			TreeIter iter = TreeIter.Zero;
			
			/* stores the segments of the last inserted uri */
			string[] last_segments = new string[] {};
			
			int last_count = 0;
			
			SqliteDataReader reader = database.Database.Query (query_string);
			
			while (reader.Read ()) {
				Uri base_uri = new Uri (reader["base_uri"].ToString ());
				
				if ( ! base_uri.IsAbsoluteUri) {
					FSpot.Utils.Log.Error ("Uri must be absolute: {0}", base_uri.ToString ());
					continue;
				}
				
				int count = Convert.ToInt32 (reader["count"]);
				
				string[] segments = base_uri.Segments;

				/* 
				 * since we have an absolute uri, first segement starts with "/" according
				 * to the msdn doc. So we can overwrite the first segment for our needs and
				 * put the scheme here.
				 */
				segments[0] = base_uri.Scheme;
				
				int i = 0;
				
				/* find first difference of last inserted an current uri */
				while (i < last_segments.Length && i < segments.Length) {
					
					/* remove suffix '/', which are appended to every directory (see msdn-doc) */
					segments[i] = segments[i].TrimEnd ('/');
					
					if (segments[i] != last_segments[i])
						break;
					
					i++;
				}
				
				/* points to the parent node of the current iter */
				TreeIter parent_iter = iter;
				
				/* step back to the level, where the difference occur */
				for (int j = 0; j + i < last_segments.Length; j++) {
					
					iter = parent_iter;
					
					if (IterParent (out parent_iter, iter)) { 
						last_count += (int)GetValue (parent_iter, 1);
						SetValue (parent_iter, 1, last_count);
					} else
						count_all += (int)last_count;
				}
				
				while (i < segments.Length) {
					segments[i] = segments[i].TrimEnd ('/');
					
					if (IterIsValid (parent_iter))
						iter =
							AppendValues (parent_iter,
							              segments[i],
							              (segments.Length - 1 == i)? count : 0,
							              new Uri ((Uri) GetValue (parent_iter, 2),
							                       String.Format ("{0}/", segments[i]))
							              );
					else
						iter =
							AppendValues (segments[i],
							              (segments.Length - 1 == i)? count : 0,
							              new Uri (base_uri, "/"));
					
					parent_iter = iter;
					
					i++;
				}
				
				last_count = count;
				last_segments = segments;
				
			}
			
			/* and at least, step back and update photo count */
			while (IterParent (out iter, iter)) {
				last_count += (int)GetValue (iter, 1);
				SetValue (iter, 1, last_count);
			}
			count_all += (int)last_count;
		}
	}
}
