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



//-----------------------------------------------------------------------------
//
//	class InputFile
//
//-----------------------------------------------------------------------------

#include <ImfInputFile.h>
#include <ImfScanLineInputFile.h>
#include <ImfTiledInputFile.h>
#include <ImfMisc.h>
#include <Iex.h>
#include <fstream>
#include <algorithm>
#include <ImfVersion.h>

#if defined PLATFORM_WIN32
namespace
{
template<class T>
inline T min (const T &a, const T &b) { return (a <= b) ? a : b; }

template<class T>
inline T max (const T &a, const T &b) { return (a >= b) ? a : b; }
}
#endif

namespace Imf {

using std::ifstream;

InputFile::InputFile (const char fileName[]):
    _sFile(0), _tFile(0)
{
    try
    {
        _fileName = fileName;
#ifndef HAVE_IOS_BASE
        _is.open (fileName, std::ios::binary|std::ios::in);
#else
        _is.open (fileName, std::ios_base::binary);
#endif
        if (!_is)
            Iex::throwErrnoExc();

        _header.readFrom (_is, _version);
        _header.sanityCheck(isTiled(_version));

        if (isTiled (_version))
        {
            _tFile = new TiledInputFile (fileName, _header, _is);
        }
        else
        {
            _sFile = new ScanLineInputFile (fileName, _header, _is);
        }
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Cannot read image file \"" << fileName << "\". " << e);
	throw;
    }
}


InputFile::~InputFile ()
{
    delete _tFile;
    delete _sFile;
}


const char *
InputFile::fileName () const
{
    return _fileName.c_str();
}


const Header &
InputFile::header () const
{
    return _header;
}


int
InputFile::version () const
{
    return _version;
}


void
InputFile::setFrameBuffer (const FrameBuffer &frameBuffer)
{
    if (isTiled (_version))
    {
	return _tFile->setFrameBuffer(frameBuffer);
    }
    else
    {
	return _sFile->setFrameBuffer(frameBuffer);
    }
}


const FrameBuffer &
InputFile::frameBuffer () const
{
    if (isTiled (_version))
    {
	return _tFile->frameBuffer();
    }
    else
    {
	return _sFile->frameBuffer();
    }
}


void
InputFile::readPixels (int scanLine1, int scanLine2)
{
    if (isTiled (_version))
    {
	_tFile->readPixels(scanLine1, scanLine2);
    }
    else
    {
	_sFile->readPixels(scanLine1, scanLine2);
    }
}


void
InputFile::readPixels (int scanLine)
{
    readPixels (scanLine, scanLine);
}


void
InputFile::rawPixelData (int firstScanLine,
			 const char *&pixelData,
			 int &pixelDataSize)
{
    try
    {
	if (isTiled (_version))
	{
	    throw Iex::ArgExc ("Tried to read a raw scanline from a tiled "
				" image.");
	}
        
        _sFile->rawPixelData(firstScanLine, pixelData, pixelDataSize);
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error reading pixel data from image "
		        "file \"" << fileName() << "\". " << e);
	throw;
    }
}

void
InputFile::rawTileData (int &dx, int &dy, int &lx, int &ly,
				      const char *&pixelData,
				      int &pixelDataSize)
{
    try
    {
	if (!isTiled (_version))
	{
	    throw Iex::ArgExc ("Tried to read a raw tile from a scanline "
				" based image.");
	}
        
        _tFile->rawTileData(dx, dy, lx, ly, pixelData, pixelDataSize);
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error reading tile data from image "
		        "file \"" << fileName() << "\". " << e);
	throw;
    }
}


} // namespace Imf
