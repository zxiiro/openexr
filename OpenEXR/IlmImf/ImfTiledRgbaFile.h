///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2003, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////



#ifndef INCLUDED_IMF_TILED_RGBA_FILE_H
#define INCLUDED_IMF_TILED_RGBA_FILE_H


//-----------------------------------------------------------------------------
//
//	Simplified RGBA image I/O for tiled files
//
//	class Rgba
//	class RgbaOutputFile
//	class RgbaInputFile
//
//-----------------------------------------------------------------------------

#include <ImfHeader.h>
#include <ImfFrameBuffer.h>
#include <ImathVec.h>
#include <ImathBox.h>
#include <half.h>
//#include <ImfRgbaFile.h>
#include <ImfTileDescription.h>
#include <ImfRgba.h>

namespace Imf {

class TiledOutputFile;
class TiledInputFile;
class PreviewRgba;

//
// RGBA output file.
//

class TiledRgbaOutputFile
{
  public:

    //---------------------------------------------------
    // Constructor -- rgbaChannels, tileXSize, tileYSize
    // and levelMode overwrite the channel list and tile
    // description attribute in the header that is passed
    // as an argument to the constructor
    //---------------------------------------------------

    TiledRgbaOutputFile (const char name[],
			 const Header &header,
			 RgbaChannels rgbaChannels,
			 int tileXSize,
			 int tileYSize,
			 LevelMode mode);

    //------------------------------------------------------
    // Constructor -- header data are explicitly specified
    // as function call arguments (an empty dataWindow means
    // "same as displayWindow")
    //------------------------------------------------------

    TiledRgbaOutputFile (const char name[],
			 int tileXSize,
			 int tileYSize,
			 LevelMode mode,
			 const Imath::Box2i &displayWindow,
			 const Imath::Box2i &dataWindow = Imath::Box2i(),
			 RgbaChannels rgbaChannels = WRITE_RGBA,
			 float pixelAspectRatio = 1,
			 const Imath::V2f screenWindowCenter =
						    Imath::V2f (0, 0),
			 float screenWindowWidth = 1,
			 LineOrder lineOrder = INCREASING_Y,
			 Compression compression = PIZ_COMPRESSION);


    //-----------------------------------------------
    // Constructor -- like the previous one, but both
    // the display window and the data window are
    // Box2i (V2i (0, 0), V2i (width - 1, height -1))
    //-----------------------------------------------

    TiledRgbaOutputFile (const char name[],
			 int width,
			 int height,
			 int tileXSize,
			 int tileYSize,
			 LevelMode mode,
			 RgbaChannels rgbaChannels = WRITE_RGBA,
			 float pixelAspectRatio = 1,
			 const Imath::V2f screenWindowCenter =
						    Imath::V2f (0, 0),
			 float screenWindowWidth = 1,
			 LineOrder lineOrder = INCREASING_Y,
			 Compression compression = PIZ_COMPRESSION);


    virtual ~TiledRgbaOutputFile ();

    //------------------------------------------------
    // Define a frame buffer as the pixel data source:
    // Pixel (x, y) is at address
    //
    //  base + x * xStride + y * yStride
    //
    //------------------------------------------------

    void		setFrameBuffer (const Rgba *base,
					size_t xStride,
					size_t yStride);

    //--------------------------
    // Access to the file header
    //--------------------------

    const Header &		header () const;
    const FrameBuffer &		frameBuffer () const;
    const Imath::Box2i &	displayWindow () const;
    const Imath::Box2i &	dataWindow () const;
    float			pixelAspectRatio () const;
    const Imath::V2f		screenWindowCenter () const;
    float			screenWindowWidth () const;
    LineOrder			lineOrder () const;
    Compression			compression () const;
    RgbaChannels		channels () const;


    //--------------------------------------------------
    // Utility functions:
    //--------------------------------------------------

    //---------------------------------------------------------
    // Multiresolution mode and tile size:
    // The following functions return the xSize, ySize and mode
    // fields of the file header's TileDescriptionAttribute.
    //---------------------------------------------------------

    unsigned int	tileXSize () const;
    unsigned int	tileYSize () const;
    LevelMode		levelMode () const;


    //------------------------------------------------------------------
    // Numer of levels:
    //
    // numXLevels() returns the file's number of levels in x direction.
    //
    // If levelMode() == ONE_LEVEL:
    //      return value is: 1
    //
    // If levelMode() == MIPMAP_LEVELS:
    //      return value is: floor (log (max (w, h)) / log (2)) + 1
    //
    // If levelMode() == RIPMAP_LEVELS:
    //      return value is: floor (log (w) / log (2)) + 1
    //
    // -----
    //
    // numYLevels() returns the file's number of levels in y direction.
    //
    // If levelMode() == ONE_LEVEL or levelMode() == MIPMAP_LEVELS:
    //      return value is the same as for numXLevels()
    //
    // If levelMode() == RIPMAP_LEVELS:
    //      return value is: floor (log (h) / log (2)) + 1
    //
    // -----
    //
    // numLevels() is a convenience function for use with MIPMAP_LEVELS
    // files.
    //
    // If levelMode() == ONE_LEVEL or levelMode() == MIPMAP_LEVELS:
    //      return value is the same as for numXLevels()
    //
    // If levelMode() == RIPMAP_LEVELS:
    //      an Iex::LogicExc exception is thrown
    //
    //------------------------------------------------------------------

    int			numLevels () const;
    int			numXLevels () const;
    int			numYLevels () const;


    //-------------------------------------------------------------
    // Dimensions of a level:
    //
    // levelWidth(lx) returns the width of a level with level
    // number (lx, *), where * is any number.
    //
    // return value is:
    //      floor (w / pow (2, lx))
    //
    // -----
    //
    // levelHeight(ly) returns the height of a level with level
    // number (*, ly), where * is any number.
    //
    // return value is:
    //      floor (h / pow (2, ly))
    //
    //-------------------------------------------------------------

    int			levelWidth (int lx) const;
    int			levelHeight(int ly) const;


    //--------------------------------------------------------------
    // Numer of tiles:
    //
    // numXTiles(lx) returns the number of tiles in x direction
    // that cover a level with level number (lx, *), where * is
    // any number.
    //
    // return value is:
    //      (levelWidth(lx) + tileXSize() - 1) / tileXSize()
    //
    // -----
    //
    // numYTiles(ly) returns the number of tiles in y direction
    // that cover a level with level number (*, ly), where * is
    // any number.
    //
    // return value is:
    //      (levelHeight(ly) + tileXSize() - 1) / tileXSize()
    //
    //--------------------------------------------------------------

    int			numXTiles (int lx = 0) const;
    int			numYTiles (int ly = 0) const;


    //-------------------------------------------------------------------
    // Level pixel ranges:
    //
    // dataWindowForLevel(lx, ly) returns a 2-dimensional
    // region of valid pixel coordinates for a level with level number
    // (lx, ly)
    //
    // return value is a Box2i with min value:
    //      (dataWindow.min.x, dataWindow.min.y)
    //
    // and max value:
    //      (dataWindow.min.x + levelWidth(lx) - 1,
    //       dataWindow.min.y + levelHeight(ly) - 1)
    //
    // dataWindowForLevel(level) is a convenience function used
    // for ONE_LEVEL and MIPMAP_LEVELS files.  It returns
    // dataWindowForLevel(level, level).
    //
    //-------------------------------------------------------------------

    Imath::Box2i	dataWindowForLevel (int l = 0) const;
    Imath::Box2i	dataWindowForLevel (int lx, int ly) const;


    //-------------------------------------------------------------------
    // Tile pixel ranges:
    //
    // dataWindowForTile(dx, dy, lx, ly) returns a 2-dimensional
    // region of valid pixel coordinates for a tile with tile coordinates
    // (dx,dy) and level number (lx, ly).
    //
    // return value is a Box2i with min value:
    //      (dataWindow.min.x + dx * tileXSize(),
    //       dataWindow.min.y + dy * tileYSize())
    //
    // and max value:
    //      (dataWindow.min.x + (dx + 1) * tileXSize() - 1,
    //       dataWindow.min.y + (dy + 1) * tileYSize() - 1)
    //
    // dataWindowForTile(dx, dy, level) is a convenience function
    // used for ONE_LEVEL and MIPMAP_LEVELS files.  It returns
    // dataWindowForTile(dx, dy, level, level).
    //
    //-------------------------------------------------------------------

    Imath::Box2i	dataWindowForTile (int dx, int dy,
					   int l = 0) const;
    Imath::Box2i	dataWindowForTile (int dx, int dy,
					   int lx, int ly) const;

    //------------------------------------------------------------------
    // Write pixel data:
    //
    // writeTile(dx, dy, lx, ly) writes the tile with tile
    // coordinates (dx, dy), and level number (lx, ly) to
    // the file.
    //
    //   dx must lie in the interval [0, numXTiles(lx, ly)-1]
    //   dy must lie in the interval [0, numYTiles(lx, ly)-1]
    //
    //   lx must lie in the interval [0, numXLevels()-1]
    //   ly must lie in the inverval [0, numYLevels()-1]
    //
    // writeTile(dx, dy, level) is a convenience function
    // used for ONE_LEVEL and MIPMAP_LEVEL files.  It calls
    // writeTile(dx, dy, level, level).
    //
    // Pixels that are outside the pixel coordinate range for the tile's
    // level, are never accessed by writeTile().
    //
    // Each tile in the file must be written exactly once.
    //
    //------------------------------------------------------------------

    void		writeTile (int dx, int dy, int l = 0);
    void		writeTile (int dx, int dy, int lx, int ly);


    // --------------------------------------------------------------------
    // Update the preview image (see Imf::OutputFile::updatePreviewImage())
    // --------------------------------------------------------------------

    void			updatePreviewImage (const PreviewRgba[]);

  private:

    TiledRgbaOutputFile (const TiledRgbaOutputFile &);		  // not implemented
    TiledRgbaOutputFile & operator = (const TiledRgbaOutputFile &); // not implemented

    TiledOutputFile *            _outputFile;
};



//
// RGBA input file
//

class TiledRgbaInputFile
{
  public:

    //---------------------------
    // Constructor and destructor
    //---------------------------

    TiledRgbaInputFile (const char name[]);
    virtual ~TiledRgbaInputFile ();

    //-----------------------------------------------------
    // Define a frame buffer as the pixel data destination:
    // Pixel (x, y) is at address
    //
    //  base + x * xStride + y * yStride
    //
    //-----------------------------------------------------

    void			setFrameBuffer (Rgba *base,
						size_t xStride,
						size_t yStride);


    //--------------------------
    // Access to the file header
    //--------------------------

    const Header &		header () const;
    const FrameBuffer &		frameBuffer () const;
    const Imath::Box2i &	displayWindow () const;
    const Imath::Box2i &	dataWindow () const;
    float			pixelAspectRatio () const;
    const Imath::V2f		screenWindowCenter () const;
    float			screenWindowWidth () const;
    LineOrder			lineOrder () const;
    Compression			compression () const;
    RgbaChannels		channels () const;
    const char *                fileName () const;

    //----------------------------------
    // Access to the file format version
    //----------------------------------

    int				version () const;


    //--------------------------------------------------
    // Utility functions:
    //--------------------------------------------------

    //---------------------------------------------------------
    // Multiresolution mode and tile size:
    // The following functions return the xSize, ySize and mode
    // fields of the file header's TileDescriptionAttribute.
    //---------------------------------------------------------

    unsigned int	tileXSize () const;
    unsigned int	tileYSize () const;
    LevelMode		levelMode () const;


    //------------------------------------------------------------------
    // Numer of levels:
    //
    // numXLevels() returns the file's number of levels in x direction.
    //
    // If levelMode() == ONE_LEVEL:
    //      return value is: 1
    //
    // If levelMode() == MIPMAP_LEVELS:
    //      return value is: floor (log (max (w, h)) / log (2)) + 1
    //
    // If levelMode() == RIPMAP_LEVELS:
    //      return value is: floor (log (w) / log (2)) + 1
    //
    // -----
    //
    // numYLevels() returns the file's number of levels in y direction.
    //
    // If levelMode() == ONE_LEVEL or levelMode() == MIPMAP_LEVELS:
    //      return value is the same as for numXLevels()
    //
    // If levelMode() == RIPMAP_LEVELS:
    //      return value is: floor (log (h) / log (2)) + 1
    //
    // -----
    //
    // numLevels() is a convenience function for use with MIPMAP_LEVELS
    // files.
    //
    // If levelMode() == ONE_LEVEL or levelMode() == MIPMAP_LEVELS:
    //      return value is the same as for numXLevels()
    //
    // If levelMode() == RIPMAP_LEVELS:
    //      an Iex::LogicExc exception is thrown
    //
    //------------------------------------------------------------------

    int			numLevels () const;
    int			numXLevels () const;
    int			numYLevels () const;


    //-------------------------------------------------------------
    // Dimensions of a level:
    //
    // levelWidth(lx) returns the width of a level with level
    // number (lx, *), where * is any number.
    //
    // return value is:
    //      floor (w / pow (2, lx))
    //
    // -----
    //
    // levelHeight(ly) returns the height of a level with level
    // number (*, ly), where * is any number.
    //
    // return value is:
    //      floor (h / pow (2, ly))
    //
    //-------------------------------------------------------------

    int			levelWidth (int lx) const;
    int			levelHeight(int ly) const;


    //--------------------------------------------------------------
    // Numer of tiles:
    //
    // numXTiles(lx) returns the number of tiles in x direction
    // that cover a level with level number (lx, *), where * is
    // any number.
    //
    // return value is:
    //      (levelWidth(lx) + tileXSize() - 1) / tileXSize()
    //
    // -----
    //
    // numYTiles(ly) returns the number of tiles in y direction
    // that cover a level with level number (*, ly), where * is
    // any number.
    //
    // return value is:
    //      (levelHeight(ly) + tileXSize() - 1) / tileXSize()
    //
    //--------------------------------------------------------------

    int			numXTiles (int lx = 0) const;
    int			numYTiles (int ly = 0) const;


    //-------------------------------------------------------------------
    // Level pixel ranges:
    //
    // dataWindowForLevel(lx, ly) returns a 2-dimensional
    // region of valid pixel coordinates for a level with level number
    // (lx, ly)
    //
    // return value is a Box2i with min value:
    //      (dataWindow.min.x, dataWindow.min.y)
    //
    // and max value:
    //      (dataWindow.min.x + levelWidth(lx) - 1,
    //       dataWindow.min.y + levelHeight(ly) - 1)
    //
    // dataWindowForLevel(level) is a convenience function used
    // for ONE_LEVEL and MIPMAP_LEVELS files.  It returns
    // dataWindowForLevel(level, level).
    //
    //-------------------------------------------------------------------

    Imath::Box2i	dataWindowForLevel (int l = 0) const;
    Imath::Box2i	dataWindowForLevel (int lx, int ly) const;


    //-------------------------------------------------------------------
    // Tile pixel ranges:
    //
    // dataWindowForTile(dx, dy, lx, ly) returns a 2-dimensional
    // region of valid pixel coordinates for a tile with tile coordinates
    // (dx,dy) and level number (lx, ly).
    //
    // return value is a Box2i with min value:
    //      (dataWindow.min.x + dx * tileXSize(),
    //       dataWindow.min.y + dy * tileYSize())
    //
    // and max value:
    //      (dataWindow.min.x + (dx + 1) * tileXSize() - 1,
    //       dataWindow.min.y + (dy + 1) * tileYSize() - 1)
    //
    // dataWindowForTile(dx, dy, level) is a convenience function
    // used for ONE_LEVEL and MIPMAP_LEVELS files.  It returns
    // dataWindowForTile(dx, dy, level, level).
    //
    //-------------------------------------------------------------------

    Imath::Box2i	dataWindowForTile (int dx, int dy, int l = 0) const;
    Imath::Box2i	dataWindowForTile (int dx, int dy, int lx, int ly) const;
					   

    //----------------------------------------------------------------
    // Read pixel data:
    //
    // readTile(dx, dy, lx, ly) reads the tile with tile
    // coordinates (dx, dy), and level number (lx, ly),
    // and stores it in the current frame buffer.
    //
    //   dx must lie in the interval [0, numXTiles(lx, ly)-1]
    //   dy must lie in the interval [0, numYTiles(lx, ly)-1]
    //
    //   lx must lie in the interval [0, numXLevels()-1]
    //   ly must lie in the inverval [0, numYLevels()-1]
    //
    // readTile(dx, dy, level) is a convenience function used
    // for ONE_LEVEL and MIPMAP_LEVELS files.  It calls
    // readTile(dx, dy, level, level).
    //
    // Pixels that are outside the pixel coordinate range for the
    // tile's level, are never accessed by readTile().
    //
    // Attempting to access a tile that is not present in the file
    // throws an InputExc exception.
    //
    //----------------------------------------------------------------

    void            readTile (int dx, int dy, int l = 0);
    void            readTile (int dx, int dy, int lx, int ly);

  private:

    TiledRgbaInputFile (const TiledRgbaInputFile &);		  // not implemented
    TiledRgbaInputFile & operator = (const TiledRgbaInputFile &);   // not implemented

    TiledInputFile *			_inputFile;
};


} // namespace Imf

#endif
