#include <glib/gi18n.h>
#include <glib.h>
#include <libgnomeui/gnome-thumbnail.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtk/gtk.h>

#include "libeog-marshal.h"
#include "eog-image.h"
#include "eog-pixbuf-util.h"

static GThread     *thread                     = NULL;
static gboolean     thread_running             = FALSE;
static GQueue      *jobs_waiting               = NULL;
static GQueue      *jobs_done                  = NULL;
static gint         dispatch_callbacks_id      = -1;
static GStaticMutex jobs_mutex                 = G_STATIC_MUTEX_INIT;

struct _EogImagePrivate {
	GFile *uri;
	EogImageLoadMode mode;

	GdkPixbuf *image;
	GdkPixbuf *thumbnail;
	
	gint width;
	gint height;

	gint load_idle_id;
	gint thumbnail_id;
	
	gboolean modified;
};

enum {
	SIGNAL_LOADING_UPDATE,
	SIGNAL_LOADING_SIZE_PREPARED,
	SIGNAL_LOADING_FINISHED,
	SIGNAL_LOADING_FAILED,
	SIGNAL_LOADING_CANCELLED,
	SIGNAL_CHANGED,
	SIGNAL_THUMBNAIL_FINISHED,
	SIGNAL_THUMBNAIL_FAILED,
	SIGNAL_THUMBNAIL_CANCELLED,
	SIGNAL_LAST
};

static gint eog_image_signals [SIGNAL_LAST];

#define NO_DEBUG
#define DEBUG_ASYNC 0
#define THUMB_DEBUG 0

/*============================================

  static thumbnail loader for all image objects

  ------------------------------------------*/

static gint
dispatch_image_finished (gpointer data)
{
	EogImage *image;
 
#if DEBUG_ASYNC
	g_print ("*** dispatch callback called ***");
#endif

	image = NULL;

	g_static_mutex_lock (&jobs_mutex);
	if (!g_queue_is_empty (jobs_done)) {
		image = EOG_IMAGE (g_queue_pop_head (jobs_done));
	}
	else {
		g_queue_free (jobs_done);
		jobs_done = NULL;
		dispatch_callbacks_id = -1;
	}
	g_static_mutex_unlock (&jobs_mutex);	

	if (image == NULL) {
#if DEBUG_ASYNC
		g_print (" --- shutdown\n");
#endif
		return FALSE;
	}
		
	if (image->priv->thumbnail != NULL) {
		g_signal_emit (G_OBJECT (image), eog_image_signals [SIGNAL_THUMBNAIL_FINISHED], 0);
	}
	else {
		g_signal_emit (G_OBJECT (image), eog_image_signals [SIGNAL_THUMBNAIL_FAILED], 0);
	}

#if DEBUG_ASYNC
	g_print ("\n");
#endif
	
	return TRUE;
}

//partly from libsoup/soup-date.c
static time_t
g_time_val_to_time_t (GTimeVal *timeval)
{
	GDate *date = g_date_new ();
	GDate *epoch = g_date_new_dmy (1, 1, 1970);
	g_date_set_time_val (date, timeval);
	
	if (date->year < 1970)
		return 0;

	if (sizeof (time_t) == 4 && date->year > 2038)
		return (time_t)0x7fffffff;
	
	time_t tt = 24 * 3600 * g_date_days_between (epoch, date);
	g_free (date);
	g_free (epoch);
	return tt;
}

static gpointer
create_thumbnails (gpointer data)
{
	EogImage *image;
	EogImagePrivate *priv;
	char *uri_str = NULL;
	char *path = NULL;
	gboolean finished = FALSE;

#if DEBUG_ASYNC
	g_print ("*** Start thread ***\n");
#endif	

	while (!finished) {

		/* get next image to process */
		g_static_mutex_lock (&jobs_mutex);

		image = EOG_IMAGE (g_queue_pop_head (jobs_waiting));
		g_assert (image != NULL);

		g_static_mutex_unlock (&jobs_mutex);

		/* thumbnail loading/creation  */

		priv = image->priv;

		uri_str = g_file_get_uri (priv->uri);
#if THUMB_DEBUG
		g_message ("uri:  %s", uri_str);
#endif
		path = gnome_thumbnail_path_for_uri (uri_str, GNOME_THUMBNAIL_SIZE_NORMAL);

#if THUMB_DEBUG
		g_message ("thumb path: %s", path);
#endif
		
		if (g_file_test (path, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
			priv->thumbnail = gdk_pixbuf_new_from_file (path, NULL);
		}
		else {
			GnomeThumbnailFactory *factory;
			GFileInfo *info;
			GError *err = NULL;

			info = g_file_query_info (priv->uri,
						  "standard::fast-content-type,time::modified",
						  G_FILE_QUERY_INFO_NONE,
						  NULL,
						  &err);

			if (err == NULL) {
#if THUMB_DEBUG
				g_print ("uri: %s, mtime: %i, mime_type %s\n", uri_str, info->mtime, info->mime_type);
#endif
				
				factory = gnome_thumbnail_factory_new (GNOME_THUMBNAIL_SIZE_NORMAL);
				GTimeVal *mtime;
				g_file_info_get_modification_time (info, mtime);
				time_t mtime_t = g_time_val_to_time_t (mtime);
				g_free (mtime);
				
				if (!gnome_thumbnail_factory_has_valid_failed_thumbnail (factory, uri_str, mtime_t) &&
				    gnome_thumbnail_factory_can_thumbnail (factory, uri_str, g_file_info_get_content_type (info), mtime_t)) 
				{
					priv->thumbnail = gnome_thumbnail_factory_generate_thumbnail (factory, uri_str, g_file_info_get_content_type(info));
					
					if (priv->thumbnail != NULL) {
						gnome_thumbnail_factory_save_thumbnail (factory, priv->thumbnail, uri_str, mtime_t);
					}
				}
				
				g_object_unref (factory);
			}
			else {
#if THUMB_DEBUG
				g_print ("uri: %s vfs errror: %s\n", uri_str, gnome_vfs_result_to_string (result));
#endif
			}
			
			g_object_unref (info);
		}
		
		g_free (uri_str);
		g_free (path);
		

		/* check for thread shutdown */
		g_static_mutex_lock (&jobs_mutex);

		if (jobs_done == NULL) {
			jobs_done = g_queue_new ();
		}
		g_queue_push_tail (jobs_done, image);
		
		if (dispatch_callbacks_id == -1) {
			dispatch_callbacks_id = g_idle_add (dispatch_image_finished, NULL);
		}

		if (g_queue_is_empty (jobs_waiting)) {
			g_queue_free (jobs_waiting);
			jobs_waiting = NULL;
			thread_running = FALSE;
			finished = TRUE;
		}
			
		g_static_mutex_unlock (&jobs_mutex);
	}

#if DEBUG_ASYNC
	g_print ("*** Finish thread ***\n");
#endif	


	return NULL;
}

static void
add_image_to_queue (EogImage *image)
{
	if (!g_thread_supported ()) {
		g_thread_init (NULL);
	}

	g_static_mutex_lock (&jobs_mutex);

	if (jobs_waiting == NULL) {
		jobs_waiting = g_queue_new ();
	}

	g_queue_push_tail (jobs_waiting, image);

	if (!thread_running) {
		thread = g_thread_create (create_thumbnails, NULL, TRUE, NULL);
		thread_running = TRUE;
	}

	g_static_mutex_unlock (&jobs_mutex);
}


/*======================================

   EogImage implementation 

   ------------------------------------*/

G_DEFINE_TYPE (EogImage, eog_image, G_TYPE_OBJECT);

static void
eog_image_dispose (GObject *object)
{
	EogImagePrivate *priv;

	priv = EOG_IMAGE (object)->priv;

	if (priv->uri) {
		g_object_unref (priv->uri);
		priv->uri = NULL;
	}

	if (priv->image) {
		g_object_unref (priv->image);
		priv->image = NULL;
	}
}

static void
eog_image_finalize (GObject *object)
{
	EogImagePrivate *priv;

	priv = EOG_IMAGE (object)->priv;

	g_free (priv);
}

static void
eog_image_class_init (EogImageClass *klass)
{
	GObjectClass *object_class = (GObjectClass*) klass;

	object_class->dispose = eog_image_dispose;
	object_class->finalize = eog_image_finalize;

	eog_image_signals [SIGNAL_LOADING_UPDATE] = 
		g_signal_new ("loading_update",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, loading_update),
			      NULL, NULL,
			      libeog_marshal_VOID__INT_INT_INT_INT,
			      G_TYPE_NONE, 4,
			      G_TYPE_INT,
			      G_TYPE_INT,
			      G_TYPE_INT,
			      G_TYPE_INT);
	eog_image_signals [SIGNAL_LOADING_SIZE_PREPARED] = 
		g_signal_new ("loading_size_prepared",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, loading_size_prepared),
			      NULL, NULL,
			      libeog_marshal_VOID__INT_INT,
			      G_TYPE_NONE, 2,
			      G_TYPE_INT,
			      G_TYPE_INT);
	eog_image_signals [SIGNAL_LOADING_FINISHED] = 
		g_signal_new ("loading_finished",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, loading_finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);			     
	eog_image_signals [SIGNAL_LOADING_FAILED] = 
		g_signal_new ("loading_failed",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, loading_failed),
			      NULL, NULL,
			      libeog_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
	eog_image_signals [SIGNAL_LOADING_CANCELLED] = 
		g_signal_new ("loading_cancelled",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, loading_cancelled),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	eog_image_signals [SIGNAL_CHANGED] = 
		g_signal_new ("changed",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	eog_image_signals [SIGNAL_THUMBNAIL_FINISHED] = 
		g_signal_new ("thumbnail_finished",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, thumbnail_finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);			     
	eog_image_signals [SIGNAL_THUMBNAIL_FAILED] = 
		g_signal_new ("thumbnail_failed",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, thumbnail_failed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	eog_image_signals [SIGNAL_THUMBNAIL_CANCELLED] = 
		g_signal_new ("thumbnail_cancelled",
			      G_TYPE_OBJECT,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EogImageClass, thumbnail_cancelled),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
eog_image_init (EogImage *img)
{
	EogImagePrivate *priv;

	priv = g_new0 (EogImagePrivate, 1);

	priv->uri = NULL;
	priv->image = NULL;
	priv->thumbnail = NULL;
	priv->width = priv->height = -1;
	priv->modified = FALSE;

	img->priv = priv;
}

EogImage* 
eog_image_new_uri (GFile *uri, EogImageLoadMode mode)
{
	EogImage *img;
	EogImagePrivate *priv;
	
	img = EOG_IMAGE (g_object_new (EOG_TYPE_IMAGE, NULL));
	priv = img->priv;

	priv->uri = g_object_ref (uri);
	priv->mode = mode;
	priv->modified = FALSE;
	
	return img;
}

EogImage* 
eog_image_new (const char *txt_uri, EogImageLoadMode mode)
{
	GFile *uri;
	EogImage *image;

	uri = g_file_new_for_uri (txt_uri);
	image = eog_image_new_uri (uri, mode);
	g_object_unref (uri);

	return image;
}

GQuark
eog_image_error_quark (void)
{
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string ("eog-image-error-quark");
	
	return q;
}


static void 
load_area_updated (GdkPixbufLoader *loader, gint x, gint y, gint width, gint height, gpointer data)
{
	EogImage *img;
	EogImagePrivate *priv;

	img = EOG_IMAGE (data);

#ifdef DEBUG
	g_print ("load_area_updated\n");
#endif

	priv = img->priv;

	if (priv->image == NULL) {
		priv->image = gdk_pixbuf_loader_get_pixbuf (loader);
		g_object_ref (priv->image);
	}

#ifdef DEBUG
	g_print ("area_updated: x: %i, y: %i, width: %i, height: %i\n", x, y, width, height);
#endif

	g_signal_emit (img, eog_image_signals [SIGNAL_LOADING_UPDATE], 0, x, y, width, height);
}

static void
load_size_prepared (GdkPixbufLoader *loader, gint width, gint height, gpointer data)
{
	EogImage *img;

	g_return_if_fail (EOG_IS_IMAGE (data));
	
	img = EOG_IMAGE (data);

	img->priv->width = width;
	img->priv->height = height;

	g_signal_emit (img, eog_image_signals [SIGNAL_LOADING_SIZE_PREPARED], 0, width, height);
}

static gboolean
real_image_load (gpointer data)
{
	EogImage *img;
	EogImagePrivate *priv;
	GdkPixbufLoader *loader;
	guchar *buffer;
	gssize bytes_read;
	gboolean failed;
	GError *err = NULL;
	GFileInputStream *inputstream;

	img = EOG_IMAGE (data);
	priv = img->priv;

#ifdef DEBUG
	g_print ("real image load %s\n", gnome_vfs_uri_to_string (priv->uri, GNOME_VFS_URI_HIDE_NONE));
#endif

	g_assert (priv->image == NULL);


	inputstream = g_file_read (priv->uri, NULL, &err);
	if (err == NULL) {
		g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_LOADING_FAILED], 0, err->message);
		g_print ("g_file_read error: %s\n", err->message);
		return FALSE;
	}
	
	buffer = g_new0 (guchar, 4096);
	loader = gdk_pixbuf_loader_new ();
	failed = FALSE;

	if (priv->mode == EOG_IMAGE_LOAD_PROGRESSIVE) {
		g_signal_connect (G_OBJECT (loader), "area-updated", (GCallback) load_area_updated, img);
		g_signal_connect (G_OBJECT (loader), "size-prepared", (GCallback) load_size_prepared, img);
	}
	
	while (TRUE) {
		bytes_read = g_input_stream_read (G_INPUT_STREAM (inputstream), buffer, 4096, NULL, &err);
//		result = gnome_vfs_read (handle, buffer, 4096, &bytes_read);
		if (bytes_read == 0) {
			break;
		}
		else if (err != NULL) {
			failed = TRUE;
			break;
		}
		
		if (!gdk_pixbuf_loader_write (loader, buffer, bytes_read, NULL)) {
			failed = TRUE;
			break;
		}

		if (priv->mode == EOG_IMAGE_LOAD_PROGRESSIVE) {
			while (gtk_events_pending ()) {
				gtk_main_iteration ();
			}
		}
	}

	g_free (buffer);
	g_input_stream_close (G_INPUT_STREAM(inputstream), NULL, NULL);
	
	if (failed) {
		if (priv->image != NULL) {
			g_object_unref (priv->image);
			priv->image = NULL;
		}

		g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_LOADING_FAILED], 0);
	}
	else {
		if (priv->image == NULL) {
			priv->image = gdk_pixbuf_loader_get_pixbuf (loader);
			g_object_ref (priv->image);

			priv->width = gdk_pixbuf_get_width (priv->image);
			priv->height = gdk_pixbuf_get_height (priv->image);
			g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_LOADING_SIZE_PREPARED], 
				       0, priv->width, priv->height);
		}
		
		g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_LOADING_FINISHED], 0);
	}

	gdk_pixbuf_loader_close (loader, NULL);	
	priv->load_idle_id = 0;

	return FALSE;
}

gboolean 
eog_image_load (EogImage *img)
{
	EogImagePrivate *priv;

	priv = EOG_IMAGE (img)->priv;

	g_return_val_if_fail (priv->uri != NULL, FALSE);

	if (priv->image == NULL && priv->load_idle_id == 0)
	{
		if (priv->mode == EOG_IMAGE_LOAD_DEFAULT) {
//			if (gnome_vfs_uri_is_local (priv->uri)) {
//				GnomeVFSFileInfo *info;
//				GnomeVFSResult result;
//				info = gnome_vfs_file_info_new ();
//				
//				result = gnome_vfs_get_file_info_uri (priv->uri,
//								      info,
//								      GNOME_VFS_FILE_INFO_DEFAULT);
//
//				if (result != GNOME_VFS_OK) {
//					g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_LOADING_FAILED], 
//						       0, gnome_vfs_result_to_string (result));
//					g_print ("VFS Error: %s\n", gnome_vfs_result_to_string (result));
//					return FALSE;
//				}
//
//				priv->mode = EOG_IMAGE_LOAD_PROGRESSIVE;
//				if (((info->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) != 0) && 
//				    (info->size < 1000000))
//				{
//					priv->mode = EOG_IMAGE_LOAD_COMPLETE;
//				}
//
//				gnome_vfs_file_info_unref (info);
//			}
//			else {
				priv->mode = EOG_IMAGE_LOAD_PROGRESSIVE;
//			}
		}
		
		priv->load_idle_id = g_idle_add (real_image_load, img);
	}
	
	return (priv->image != NULL);
}

gboolean 
eog_image_load_thumbnail (EogImage *img)
{
	EogImagePrivate *priv;

	g_return_val_if_fail (EOG_IS_IMAGE (img), FALSE);

	priv = img->priv;

	if (priv->thumbnail == NULL)
	{
		add_image_to_queue (img);
	}
	
	return (priv->thumbnail != NULL);
}

gboolean 
eog_image_is_animation (EogImage *img)
{
	return FALSE;
}

GdkPixbuf* 
eog_image_get_pixbuf (EogImage *img)
{
	g_return_val_if_fail (EOG_IS_IMAGE (img), NULL);

	if (img->priv->image != 0) {
		g_object_ref (img->priv->image);
		return img->priv->image;
	}
	else {
		return 0;
	}
}

GdkPixbuf* 
eog_image_get_pixbuf_thumbnail (EogImage *img)
{
	g_return_val_if_fail (EOG_IS_IMAGE (img), NULL);

	if (img->priv->thumbnail != 0) {
		g_object_ref (img->priv->thumbnail);
		return img->priv->thumbnail;
	}

	return NULL;
}

void 
eog_image_get_size (EogImage *img, int *width, int *height)
{
	EogImagePrivate *priv;

	g_return_if_fail (EOG_IS_IMAGE (img));

	priv = img->priv;

	*width = priv->width; 
	*height = priv->height;
}


void    
eog_image_rotate_clock_wise (EogImage *img)
{
	EogImagePrivate *priv;
	GdkPixbuf *rotated;

	g_return_if_fail (EOG_IS_IMAGE (img));

	priv = img->priv;
	if (priv->image == NULL) return;

	rotated = eog_pixbuf_rotate_90_cw (priv->image);
	g_object_unref (priv->image);
	priv->image = rotated;

	priv->modified = TRUE;
	g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_CHANGED], 0);
}

void    
eog_image_rotate_counter_clock_wise (EogImage *img)
{
	EogImagePrivate *priv;
	GdkPixbuf *rotated;

	g_return_if_fail (EOG_IS_IMAGE (img));

	priv = img->priv;
	if (priv->image == NULL) return;

	rotated = eog_pixbuf_rotate_90_ccw (priv->image);
	g_object_unref (priv->image);
	priv->image = rotated;

	priv->modified = TRUE;
	g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_CHANGED], 0);	
}

void
eog_image_rotate_180 (EogImage *img)
{
	EogImagePrivate *priv;

	g_return_if_fail (EOG_IS_IMAGE (img));
	
	priv = img->priv;
	if (priv->image == NULL) return;

	eog_pixbuf_rotate_180 (priv->image);
	
	priv->modified = TRUE;
	g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_CHANGED], 0);
}

void
eog_image_flip_horizontal (EogImage *img)
{
	EogImagePrivate *priv;
	
	g_return_if_fail (EOG_IS_IMAGE (img));

	priv = img->priv;
	if (priv->image == NULL) return;

	eog_pixbuf_flip_horizontal (priv->image);

	priv->modified = TRUE;
	g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_CHANGED], 0);	
}

void
eog_image_flip_vertical (EogImage *img)
{
	EogImagePrivate *priv;
	
	g_return_if_fail (EOG_IS_IMAGE (img));

	priv = img->priv;
	if (priv->image == NULL) return;

	eog_pixbuf_flip_vertical (priv->image);

	priv->modified = TRUE;
	g_signal_emit (G_OBJECT (img), eog_image_signals [SIGNAL_CHANGED], 0);	
}

//gboolean
//eog_image_save (EogImage *img, const GFile *uri, GError **error)
//{
//	EogImagePrivate *priv;
//	char *file;
//	char *file_type = NULL;
//
//	g_return_val_if_fail (EOG_IS_IMAGE (img), FALSE);
//	g_return_val_if_fail (uri != NULL, FALSE);
//	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
//
//	priv = img->priv;
//
//	if (priv->image == NULL) {
//		g_set_error (error, EOG_IMAGE_ERROR,
//			     EOG_IMAGE_ERROR_NOT_LOADED,
//			     _("No image loaded."));
//		return FALSE;
//	}
//	
//	if (!gnome_vfs_uri_is_local (uri)) {
//		g_set_error (error, EOG_IMAGE_ERROR, 
//			     EOG_IMAGE_ERROR_SAVE_NOT_LOCAL,
//			     _("Can't save non local files."));
//		return FALSE;
//	}
//
//	file = (char*) gnome_vfs_uri_get_path (uri); /* don't free file */
//	
//	if (g_str_has_suffix (file, ".png")) {
//		file_type = "png";
//	}
//	else if (g_str_has_suffix (file, ".jpg") ||
//		 g_str_has_suffix (file, ".jpeg"))
//	{
//		file_type = "jpeg";
//	}
//#if 0
//	else if (g_str_has_suffix (file, ".xpm")) {
//		return eog_image_helper_save_xpm (priv->image, file, error);
//	}
//#endif
//
//	if (file_type == NULL) {
//		g_set_error (error, GDK_PIXBUF_ERROR,
//			     GDK_PIXBUF_ERROR_UNKNOWN_TYPE,
//			     _("Unsupported image type for saving."));
//		return FALSE;
//	}
//	else {
//		return gdk_pixbuf_save (priv->image, file, file_type, error, NULL);
//	}
//
//	return FALSE;
//}

gchar*               
eog_image_get_caption (EogImage *img)
{
	EogImagePrivate *priv;

	g_return_val_if_fail (EOG_IS_IMAGE (img), NULL);

	priv = img->priv;

	if (priv->uri == NULL)
		return NULL;

	return g_file_get_basename (priv->uri);
}

void
eog_image_free_mem (EogImage *img)
{
	EogImagePrivate *priv;
	
	g_return_if_fail (EOG_IS_IMAGE (img));
	
	priv = img->priv;

	if (priv->image != NULL) {
		g_object_unref (priv->image);
		priv->image = NULL;
	}
}
