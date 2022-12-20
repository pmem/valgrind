
/*--------------------------------------------------------------------*/
/*--- Replacements for memset, memcmp and  memcpy which run on the ---*/
/*---                   simulated CPU.                             ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of Valgrind.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

#include "pub_tool_redir.h"

/* ---------------------------------------------------------------------
   On PPC64, POWER10, the glibc-variants can read past the end of the
   input data ranges. This causes false-negative onn PMemcheck.
   --------------------------------------------*/

#define MEMSET(soname, fnname) \
   void* VG_REPLACE_FUNCTION_EZZ(20210,soname,fnname) \
            (void *s, Int c, SizeT n); \
   void* VG_REPLACE_FUNCTION_EZZ(20210,soname,fnname) \
            (void *s, Int c, SizeT n) \
   { \
      if (sizeof(void*) == 8) { \
         Addr  a  = (Addr)s;   \
         ULong c8 = (c & 0xFF); \
         c8 = (c8 << 8) | c8; \
         c8 = (c8 << 16) | c8; \
         c8 = (c8 << 32) | c8; \
         while ((a & 7) != 0 && n >= 1) \
            { *(UChar*)a = (UChar)c; a += 1; n -= 1; } \
         while (n >= 32) \
            { *(ULong*)a = c8; a += 8; n -= 8;   \
              *(ULong*)a = c8; a += 8; n -= 8;   \
              *(ULong*)a = c8; a += 8; n -= 8;   \
              *(ULong*)a = c8; a += 8; n -= 8; } \
         while (n >= 8) \
            { *(ULong*)a = c8; a += 8; n -= 8; } \
         while (n >= 1) \
            { *(UChar*)a = (UChar)c; a += 1; n -= 1; } \
         return s; \
      } else { \
         Addr a  = (Addr)s;   \
         UInt c4 = (c & 0xFF); \
         c4 = (c4 << 8) | c4; \
         c4 = (c4 << 16) | c4; \
         while ((a & 3) != 0 && n >= 1) \
            { *(UChar*)a = (UChar)c; a += 1; n -= 1; } \
         while (n >= 16) \
            { *(UInt*)a = c4; a += 4; n -= 4;   \
              *(UInt*)a = c4; a += 4; n -= 4;   \
              *(UInt*)a = c4; a += 4; n -= 4;   \
              *(UInt*)a = c4; a += 4; n -= 4; } \
         while (n >= 4) \
            { *(UInt*)a = c4; a += 4; n -= 4; } \
         while (n >= 1) \
            { *(UChar*)a = (UChar)c; a += 1; n -= 1; } \
         return s; \
      } \
   }

#define MEMCMP(soname, fnname) \
   int VG_REPLACE_FUNCTION_EZU(20190,soname,fnname)       \
          ( const void *s1V, const void *s2V, SizeT n ); \
   int VG_REPLACE_FUNCTION_EZU(20190,soname,fnname)       \
          ( const void *s1V, const void *s2V, SizeT n )  \
   { \
      const SizeT WS = sizeof(UWord); /* 8 or 4 */ \
      const SizeT WM = WS - 1;        /* 7 or 3 */ \
      Addr s1A = (Addr)s1V; \
      Addr s2A = (Addr)s2V; \
      \
      if (((s1A | s2A) & WM) == 0) { \
         /* Both areas are word aligned.  Skip over the */ \
         /* equal prefix as fast as possible. */ \
         while (n >= WS) { \
            UWord w1 = *(UWord*)s1A; \
            UWord w2 = *(UWord*)s2A; \
            if (w1 != w2) break; \
            s1A += WS; \
            s2A += WS; \
            n -= WS; \
         } \
      } \
      \
      const UChar* s1 = (const UChar*) s1A; \
      const UChar* s2 = (const UChar*) s2A; \
      \
      while (n != 0) { \
         UChar a0 = s1[0]; \
         UChar b0 = s2[0]; \
         s1 += 1; \
         s2 += 1; \
         int res = ((int)a0) - ((int)b0); \
         if (res != 0) \
            return res; \
         n -= 1; \
      } \
      return 0; \
   }


/* Figure out if [dst .. dst+dstlen-1] overlaps with
                 [src .. src+srclen-1].
   We assume that the address ranges do not wrap around
   (which is safe since on Linux addresses >= 0xC0000000
   are not accessible and the program will segfault in this
   circumstance, presumably).
*/
static inline
Bool is_overlap ( void* dst, const void* src, SizeT dstlen, SizeT srclen )
{
   Addr loS, hiS, loD, hiD;

   if (dstlen == 0 || srclen == 0)
      return False;

   loS = (Addr)src;
   loD = (Addr)dst;
   hiS = loS + srclen - 1;
   hiD = loD + dstlen - 1;

   /* So figure out if [loS .. hiS] overlaps with [loD .. hiD]. */
   if (loS < loD) {
      return !(hiS < loD);
   }
   else if (loD < loS) {
      return !(hiD < loS);
   }
   else {
      /* They start at same place.  Since we know neither of them has
         zero length, they must overlap. */
      return True;
   }
}

/*---------------------- memcpy ----------------------*/

#define MEMMOVE_OR_MEMCPY(becTag, soname, fnname, do_ol_check)  \
   void* VG_REPLACE_FUNCTION_EZZ(becTag,soname,fnname) \
            ( void *dst, const void *src, SizeT len ); \
   void* VG_REPLACE_FUNCTION_EZZ(becTag,soname,fnname) \
            ( void *dst, const void *src, SizeT len ) \
   { \
      const Addr WS = sizeof(UWord); /* 8 or 4 */ \
      const Addr WM = WS - 1;        /* 7 or 3 */ \
      \
      if (len > 0) { \
         if (dst < src || !is_overlap(dst, src, len, len)) { \
         \
            /* Copying backwards. */ \
            SizeT n = len; \
            Addr  d = (Addr)dst; \
            Addr  s = (Addr)src; \
            \
            if (((s^d) & WM) == 0) { \
               /* s and d have same UWord alignment. */ \
               /* Pull up to a UWord boundary. */ \
               while ((s & WM) != 0 && n >= 1) \
                  { *(UChar*)d = *(UChar*)s; s += 1; d += 1; n -= 1; } \
               /* Copy UWords. */ \
               while (n >= WS * 4) \
                  { *(UWord*)d = *(UWord*)s; s += WS; d += WS; n -= WS;   \
                    *(UWord*)d = *(UWord*)s; s += WS; d += WS; n -= WS;   \
                    *(UWord*)d = *(UWord*)s; s += WS; d += WS; n -= WS;   \
                    *(UWord*)d = *(UWord*)s; s += WS; d += WS; n -= WS; } \
               while (n >= WS) \
                  { *(UWord*)d = *(UWord*)s; s += WS; d += WS; n -= WS; } \
               if (n == 0) \
                  return dst; \
            } \
            if (((s|d) & 1) == 0) { \
               /* Both are 16-aligned; copy what we can thusly. */ \
               while (n >= 2) \
                  { *(UShort*)d = *(UShort*)s; s += 2; d += 2; n -= 2; } \
            } \
            /* Copy leftovers, or everything if misaligned. */ \
            while (n >= 1) \
               { *(UChar*)d = *(UChar*)s; s += 1; d += 1; n -= 1; } \
         \
         } else if (dst > src) { \
         \
            SizeT n = len; \
            Addr  d = ((Addr)dst) + n; \
            Addr  s = ((Addr)src) + n; \
            \
            /* Copying forwards. */ \
            if (((s^d) & WM) == 0) { \
               /* s and d have same UWord alignment. */ \
               /* Back down to a UWord boundary. */ \
               while ((s & WM) != 0 && n >= 1) \
                  { s -= 1; d -= 1; *(UChar*)d = *(UChar*)s; n -= 1; } \
               /* Copy UWords. */ \
               while (n >= WS * 4) \
                  { s -= WS; d -= WS; *(UWord*)d = *(UWord*)s; n -= WS;   \
                    s -= WS; d -= WS; *(UWord*)d = *(UWord*)s; n -= WS;   \
                    s -= WS; d -= WS; *(UWord*)d = *(UWord*)s; n -= WS;   \
                    s -= WS; d -= WS; *(UWord*)d = *(UWord*)s; n -= WS; } \
               while (n >= WS) \
                  { s -= WS; d -= WS; *(UWord*)d = *(UWord*)s; n -= WS; } \
               if (n == 0) \
                  return dst; \
            } \
            if (((s|d) & 1) == 0) { \
               /* Both are 16-aligned; copy what we can thusly. */ \
               while (n >= 2) \
                  { s -= 2; d -= 2; *(UShort*)d = *(UShort*)s; n -= 2; } \
            } \
            /* Copy leftovers, or everything if misaligned. */ \
            while (n >= 1) \
               { s -= 1; d -= 1; *(UChar*)d = *(UChar*)s; n -= 1; } \
            \
         } \
      } \
      \
      return dst; \
   }

#define MEMCPY(soname, fnname) \
   MEMMOVE_OR_MEMCPY(20180, soname, fnname, 1)


#if defined(VGO_linux)
 MEMCMP(VG_Z_LIBC_SONAME, __memcmp_power10)
 MEMSET(VG_Z_LIBC_SONAME, __memset_power10)
 MEMCPY(VG_Z_LIBC_SONAME, __memcpy_power10)
# endif
