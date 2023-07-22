#pragma once

class BlockStreamBuf: public std::streambuf {
private:
    Repository& repo;
    Segment segm;

public:
    BlockStreamBuf(Repository& repo, const Segment& segm): repo(repo), segm(segm) {}

    // TODO disable copy? allow move?

protected:
    // Reference:
    // https://codebrowser.dev/gcc/include/c++/9/bits/sstream.tcc.html
    // https://github.com/gcc-mirror/gcc/blob/4832767db7897be6fb5cbc44f079482c90cb95a6/libstdc%2B%2B-v3/include/std/sstream#L165
    //
    /**** Locales ***/
    // virtual void imbue(const std::locale& loc); default works fine

    /**** Positioning ****/

    // TODO: what?
    // The derived classes may override this function to allow removal or replacement
    // of the controlled character sequence (the buffer) with a user-provided array,
    // or for any other implementation-specific purpose.
    virtual basic_streambuf<CharT, Traits>* setbuf(char_type* s, std::streamsize n);

    // Sets the position indicator of the input and/or output sequence relative to some other
    // position. Return the absolute position as defined by the position indicator; return
    // pos_type(off_type(-1)) otherwise (may be to indicate an error?)
    virtual pos_type seekoff(off_type off,
                             std::ios_base::seekdir dir,  // std::ios_base::beg / cur / end
                             std::ios_base::openmode which = ios_base::in | ios_base::out);

    // Sets the position indicator of the input and/or output sequence to an absolute position.
    // Same return than in seekoff
    virtual pos_type seekpos(pos_type pos, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out);

    // Synchronizes the controlled character sequence (the buffers) with the associated character
    // sequence. For output streams, this typically results in writing the contents of the put area
    // into the associated sequence, i.e. flushing of the output buffer. For input streams, this
    // typically empties the get area and forces a re-read from the associated sequence to pick up
    // recent changes. The default behavior (found, for example, in std::basic_stringbuf), is to do
    // nothing. Returns 0 on success, -1 otherwise.
    virtual int sync();

    /**** Get area ****/

    // Non-virtual methods:
    //  - gptr() / egptr() return the pointer to
    //    the current position within the get area / the end (last valid position + 1)
    //    of the get area
    //  - eback() return the pointer to the beginning of the get area. The name "eback" refers to
    //  the end
    //    of the putback area: stepping backwards from gptr, characters can be put back until eback.
    //
    //  - void gbump( int count );
    //  Skips count characters in the get area.
    //  This is done by adding count to the get pointer. **No checks are done for underflow.**
    //
    //  - void setg( char_type* gbeg, char_type* gcurr, char_type* gend );
    //   Sets the values of the pointers defining the get area.
    //   Specifically, after the call assert(eback() == gbeg and gptr() == gcurr and egptr() ==
    //   gend)

    // Estimates the number of characters available for input in the associated character sequence.
    // underflow() is guaranteed not to return Traits::eof() or throw an exception until at
    // least that many characters are extracted.
    //
    // The number of characters that are certainly available in the associated character sequence,
    // or -1 if showmanyc can determine, without blocking, that no characters are available.
    // If showmanyc returns -1, underflow() and uflow() will definitely return Traits::eof or throw.
    //
    // The base class version returns 0, which has the meaning of
    // "unsure if there are characters available in the associated sequence".
    virtual std::streamsize showmanyc();

    // Ensures that at least one character is available in the input area by updating the pointers
    // to the input area (if needed) and reading more data in from the input sequence (if
    // applicable).
    //
    // Returns the value of that character (converted to int_type with Traits::to_int_type(c))
    // on success or Traits::eof() on failure.
    //
    // The value of the character pointed to by the get pointer **after** the call on success, or
    // Traits::eof() otherwise.
    //
    // The function may update gptr, egptr and eback pointers to define the location of newly loaded
    // data (if any). On failure, the function ensures that either gptr() == nullptr or gptr() ==
    // egptr.
    //
    // The function may be called if gptr() == nullptr or gptr() >= egptr()
    //
    virtual int_type underflow();  // TODO reads/loads a single byte?

    // Ensures that at least one character is available in the input area by updating the pointers
    // to the input area (if needed).
    // On success returns the value of that character and advances the value of the get pointer
    // by one character. On failure returns traits::eof().
    //
    // The value of the character that was pointed to by the get pointer **before** it was advanced
    // by one, or traits::eof() otherwise.
    //
    // The base class version of the function calls underflow() and increments gptr().
    //
    // The function may update gptr, egptr and eback pointers to define the location
    // of newly loaded data (if any).
    // On failure, the function ensures that assert(gptr() == nullptr or gptr() == egptr)
    //
    // The function may be called if gptr() == nullptr or gptr() >= egptr()
    //
    // The custom streambuf classes that do not use the get area and do not set the get area
    // pointers are required to override this function.
    virtual int_type uflow();

    // Reads count characters from the input sequence and stores them into a character array
    // pointed to by s. The characters are read as if by repeated calls to sbumpc().
    // That is, if less than count characters are immediately available,
    // the function calls uflow() to provide more until Traits::eof() is returned
    //
    // The number of characters successfully read. If it is less than count the input sequence has
    // reached the end.
    //
    // The rule about "more efficient implementations" permits bulk I/O without intermediate
    // buffering
    virtual std::streamsize xsgetn(char_type* s, std::streamsize count);

    /**** Put area ****/

    // These are non-virtual methods
    //
    // char_type* pbase() const; char_type* pptr() const; char_type* epptr() const;
    // Return the pointer base of the put area; the put pointer (the current char)
    // and a pointer to one past the end of the put area.
    //
    //
    // Repositions the put pointer (pptr()) by count characters, where count may be positive or
    // negative. No checks are done for moving the pointer outside the put area [pbase(), epptr()).
    //
    // If the pointer is advanced and then overflow() is called to flush the put area to the
    // associated character sequence, the effect is that extra count characters with undefined
    // values are output. void pbump( int count );
    //
    //
    // void setp( char_type* pbeg, char_type* pend );
    // Sets the values of the pointers defining the put area

    // Writes count characters to the output sequence from the character array whose first
    // element is pointed to by s. The characters are written as if by repeated calls to sputc().
    // Writing stops when either count characters are written or a call to sputc() would have
    // returned Traits::eof().
    //
    // The number of characters successfully written.
    //
    // If the put area becomes full (pptr() == epptr()), it is unspecified whether overflow()
    // is actually called or its effect is achieved by other means (like doing a bulk operation
    // directly)
    virtual std::streamsize xsputn(const char_type* s, std::streamsize count);

    // Ensures that there is space at the put area for at least one character by saving some initial
    // subsequence of characters starting at pbase() to the output sequence and updating the
    // pointers to the put area (if needed).
    //
    // If the returned ch is not Traits::eof() (i.e. Traits::eq_int_type(ch, Traits::eof()) !=
    // true), it is either put to the put area or directly saved to the output sequence.
    //
    // The function may update pptr, epptr and pbase pointers to define the location to write more
    // data. On failure, the function ensures that either pptr() == nullptr or pptr() == epptr and
    // return Traits::eof()
    //
    // The sputc() and sputn() call this function in case of an overflow (pptr() == nullptr or
    // pptr() >= epptr()).
    virtual int_type overflow(int_type ch = Traits::eof());

    /**** Putback ****/

    virtual int_type pbackfail(int_type c = Traits::eof());
};
