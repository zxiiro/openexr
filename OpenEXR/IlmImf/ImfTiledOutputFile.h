///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2002, Industrial Light & Magic, a division of Lucas
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



#ifndef INCLUDED_IMF_TILED_OUTPUT_FILE_H
#define INCLUDED_IMF_TILED_OUTPUT_FILE_H

//-----------------------------------------------------------------------------
//
//	class TiledOutputFile
//
//-----------------------------------------------------------------------------

#include <ImfHeader.h>
#include <ImfFrameBuffer.h>
#include <ImathBox.h>
#include <ImfTileDescription.h>

namespace Imf {

class TiledInputFile;
class InputFile;


class TiledOutputFile
{
  public:

    //------------------------------------------------------
    // Constructor -- opens the file and writes the file header.
    // The file header is also copied into the Tiled OutputFile object,
    // and can later be accessed via the header() method.
    //
    // Header must contain a TileDescriptionAttribute called "tiles".
    // All image channels must have sampling (1,1); subsampling is
    // not supported.
    // Line order could be used to order the tiles in the file to
    // make reading faster.
    //------------------------------------------------------
    
    TiledOutputFile (const char fileName[], const Header &header);


    //------------------------------------------------------
    // Destructor -- closes the file.
    // Closing the file before writing all scan lines within
    // the data window results in an incomplete file.
    //------------------------------------------------------
    
    virtual ~TiledOutputFile ();


    //------------------------
    // Access to the file name
    //------------------------
    
    const char *	fileName () const;


    //--------------------------
    // Access to the file header
    //--------------------------
    
    const Header &	header () const;


    //-------------------------------------------------------
    // Set the current frame buffer -- copies the FrameBuffer
    // object into the TiledOutputFile object.
    //
    // The current frame buffer is the source of the pixel
    // data written to the file.  The current frame buffer
    // must be set at least once before writeTile() is
    // called.  The current frame buffer can be changed
    // after each call to writeTile.
    //-------------------------------------------------------
    
    void		setFrameBuffer (const FrameBuffer &frameBuffer);


    //-----------------------------------
    // Access to the current frame buffer
    //-----------------------------------
    
    const FrameBuffer &	frameBuffer () const;


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
    // pixelRangeForLevel(lx, ly) returns a 2-dimensional
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
    // pixelRangeForLevel(level) is a convenience function used
    // for ONE_LEVEL and MIPMAP_LEVELS files.  It returns
    // pixelRangeForLevel(level, level).
    //
    //-------------------------------------------------------------------

    Imath::Box2i	pixelRangeForLevel (int l = 0) const;
    Imath::Box2i	pixelRangeForLevel (int lx, int ly) const;


    //-------------------------------------------------------------------
    // Tile pixel ranges:
    //
    // pixelRangeForTile(dx, dy, lx, ly) returns a 2-dimensional
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
    // pixelRangeForTile(dx, dy, level) is a convenience function
    // used for ONE_LEVEL and MIPMAP_LEVELS files.  It returns
    // pixelRangeForTile(dx, dy, level, level).
    //
    //-------------------------------------------------------------------

    Imath::Box2i	pixelRangeForTile (int dx, int dy,
					   int l = 0) const;
    Imath::Box2i	pixelRangeForTile (int dx, int dy,
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


    //--------------------------------------------------------------
    // Shortcut to copy all pixels from a TiledInputFile into this file,
    // without uncompressing and then recompressing the pixel data.
    // This file's header must be compatible with the TiledInputFile's
    // header:  The two header's "dataWindow", "compression",
    // "lineOrder", "channels", and "tiles" attributes must be the same.
    //--------------------------------------------------------------
    
    void		copyPixels (TiledInputFile &in);
    

    //--------------------------------------------------------------
    // Shortcut to copy all pixels from an InputFile into this file,
    // without uncompressing and then recompressing the pixel data.
    // This file's header must be compatible with the InputFile's
    // header:  The two header's "dataWindow", "compression",
    // "lineOrder", "channels", and "tiles" attributes must be the same.
    //
    // To use this function, the InputFile must be tiled.
    //--------------------------------------------------------------
    
    void		copyPixels (InputFile &in);


    class Data;

  private:

    TiledOutputFile (const TiledOutputFile &);		    // not implemented
    TiledOutputFile & operator = (const TiledOutputFile &); // not implemented

    bool isValidTile(int dx, int dy, int lx, int ly) const;
    size_t bytesPerLineForTile(int dx, int dy, int lx, int ly) const;

    Data *		_data;
};


} // namespace Imf

#endif
