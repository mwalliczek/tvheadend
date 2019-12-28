#
/*
 *    Copyright (C) 2015 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Programming
 *
 *    This file is part of the DAB-library
 *    DAB-library is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    DAB-library is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with DAB-library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	This charset handling was kindly added by Przemyslaw Wegrzyn	
 *	all rights acknowledged
 *
 *	Adapted to tvheadend by Matthias Walliczek (matthias@walliczek.de)
 */
#include	"charsets.h"
#include	<stdint.h>
#include	<string.h>
#include	<stdlib.h>

static const char* utf8_encoded_EBU_Latin[] = {
"\0", "E", "I", "U", "A", "E", "D", "?", "?", "C", "\n","\v","G", "L", "Z", "N",
"a", "e", "i", "u", "a", "e", "d", "?", "?", "c", "N", "E", "g", "l", "z", "\x82",
" ", "!", "\"","#", "l", "%", "&", "'", "(", ")", "*", "+", ",", "-", ".", "/",
"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", "=", ">", "?",
"@", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O",
"P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "[", "U", "]", "L", "_",
"A", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o",
"p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "�", "u", "�", "L", "H",
"�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "S", "�", "�", "�",
"�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "s", "g", "i", "�",
"K", "N", "�", "G", "G", "e", "n", "o", "O", "�", "�", "$", "A", "E", "I", "U",
"k", "n", "L", "g", "l", "I", "n", "u", "U", "�", "l", "�", "a", "e", "i", "u",
"�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "R", "C", "�", "�", "�", "?",
"�", "�", "�", "�", "�", "�", "�", "�", "�", "�", "r", "c", "�", "�", "d", "?",
"�", "�", "�", "�", "y", "�", "�", "�", "�", "?", "R", "C", "S", "Z", "T", "�",
"�", "�", "�", "�", "w", "�", "�", "�", "�", "?", "r", "c", "s", "z", "t", "h"};


char* toStringUsingCharset (const char* buffer,
	                          CharacterSet charset, int size) {
char*  s;
uint16_t length = 0;
uint16_t i;

	if (size == -1)
	   length = strlen (buffer);
	else
	   length = size;

	switch (charset) {
	   case UnicodeUcs2:
		   s = strdup("\0");
 	       break;

	   case UnicodeUtf8:
	   case IsoLatin:
	   default:
		   s = strndup(buffer, length);
	       break;

	   case EbuLatin:
		   s = malloc(length * 2);
		   *s = '\0';
		   for (i = 0; i < length; i++)
			   strcat(s, utf8_encoded_EBU_Latin [buffer[i] & 0xff]);
	      break;
	}

	return s;
}
