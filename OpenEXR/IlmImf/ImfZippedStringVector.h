///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2013, Weta Digital Ltd
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
// *       Neither the names of Weta Digital Ltd, Industrial Light & Magic
// nor the names of their contributors may be used to endorse or promote products derived
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


#ifndef INCLUDED_IMF_ZIPPED_STRING_VECTOR_H
#define INCLUDED_IMF_ZIPPED_STRING_VECTOR_H

//-----------------------------------------------------------------------------
//
// a vector of std::strings which is zipped before storing in the header, and unzipped
// before access: note this is *not* a vector of ZippedString objects
//
//-----------------------------------------------------------------------------

#include "ImfNamespace.h"
#include "ImfExport.h"
#include "ImfForward.h"
#include <vector>
#include <string>

OPENEXR_IMF_INTERNAL_NAMESPACE_HEADER_ENTER

   
class IMF_EXPORT ZippedStringVector
{
public:
    std::vector<std::string> & vec();                                   ///< access to string: may unzip if (still) compressed
    std::vector<std::string> vec() const;                               ///< access to string, returns unzipped copy of string if still compressed
    ZippedStringVector();                                               ///< initialize empty string vector
    explicit ZippedStringVector(const std::vector<std::string> & str);  ///< cast a vector of strings to a ZippedString
    explicit ZippedStringVector(const ZippedStringVector & str);        ///< copy constructor
    bool isCompressed() const;                                          ///< true if the string is already compressed
    ZippedStringVector & operator=(const ZippedStringVector & other);               ///< assignment
    bool operator==( const Imf::ZippedStringVector& other ) const;            ///< true if the string vectors are equal, even if one is compressed and the other isn't
   ~ZippedStringVector();
   
    void writeStringVector(OStream & s) const;                   ///< write string in compressed form to given stream.
    void readStringVector( Imf::IStream& s, int size );                          ///< read string: will be compressed
   
private:
    class Data;
    Data * _data;
};


OPENEXR_IMF_INTERNAL_NAMESPACE_HEADER_EXIT

#endif
