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
//	MAGIC		magic number that identifies image files
//	VERSION		the current image file format version
//
//-----------------------------------------------------------------------------


namespace Imf {


const int MAGIC = 20000630;
const int VERSION = 2;

const int VERSION_NUMBER_FIELD = 0x000000ff;
const int VERSION_FLAGS_FIELD  = 0xffffff00;

// value that goes into VERSION_NUMBER_FIELD.
const int CURRENT_VERSION      = 0x00000002;

// flags that go into VERSION_FLAGS_FIELD.
// Can only occupy the 1 bits in VERSION_FLAGS_FIELD.
const int TILED_FLAG           = 0x00000100;

// Bitwise OR of all known flags.
const int ALL_FLAGS            = TILED_FLAG;



inline bool
isTiled (int version) { return version & TILED_FLAG; }

inline int
makeTiled(int version) { return version | TILED_FLAG; }

inline int
makeNotTiled(int version) { return version & ~TILED_FLAG; }

inline int
getVersion(int version) { return version & VERSION_NUMBER_FIELD; }

inline int
getFlags(int version) { return version & VERSION_FLAGS_FIELD; }

inline bool
supportsFlags(int flag) { return !(flag & ~ALL_FLAGS); }


} // namespace Imf
