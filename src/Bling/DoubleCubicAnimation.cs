//
// FSpot.Bling.DoubleCubicAnimation,cs
//
// Author(s):
//	Stephane Delcroix  <stephane@delcroix.org>
//
// This is free software. See COPYING for details
//

using System;

namespace FSpot.Bling
{	public class DoubleCubicAnimation : CubicAnimation<double>
	{
		public DoubleCubicAnimation (double from, double to, TimeSpan duration, Action<double> action) : base (from, to, duration, action)
		{
		}

		protected override double Interpolate (double from, double to, double progress)
		{
			return from + progress * (to - from);
		}
	}
}
