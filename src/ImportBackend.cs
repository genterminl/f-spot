using Gdk;

public abstract class ImportBackend {
	// Prepare for importing; returns the number of pictures available.
	// If it returns zero, you should not call Step(), Cancel() or Finish() until you call Prepare() again.
	public abstract int Prepare ();

	// Import one picture.  Returns false when done; then you have to call Finish().
	public abstract bool Step (out Photo photo, out Pixbuf thumbnail, out int count);

	// Cancel importing.
	public abstract void Cancel ();

	// Complete importing (needs to be called).
	public abstract void Finish ();
}
