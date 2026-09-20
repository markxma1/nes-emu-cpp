///   Copyright 2016 Xma1
///
///   This file is part of NES-C#.
///
///   NES-C# is free software: you can redistribute it and/or modify
///   it under the terms of the GNU General Public License as published by
///   the Free Software Foundation, either version 3 of the License, or
///   (at your option) any later version.
///
///   NES-C# is distributed in the hope that it will be useful,
///   but WITHOUT ANY WARRANTY; without even the implied warranty of
///   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
///   See the GNU General Public License for more details.
///
///   You should have received a copy of the GNU General Public License
///   along with NES-C#. If not, see http://www.gnu.org/licenses/.
#pragma once
#include <cstdint>

namespace NES
{
    /// @brief Resolves 6502 addressing modes to a final memory address.
    /// Port of CPU/CPU/Parameter.cs. See
    /// https://en.wikibooks.org/wiki/6502_Assembly and http://nesdev.com/6502.txt
    class Parameter
    {
    public:
        /// Absolute Indexed with X: a,x
        /// The value in X is added to the specified address for a sum address.
        /// Example: ADC $C001,X - the value $02 in X is added to $C001 for a sum of $C003.
        static uint16_t ax(uint16_t ax);

        /// Absolute Indexed with Y: a,y
        /// The value in Y is added to the specified address for a sum address.
        /// Example: ADC $C001,Y - the value $02 in Y is added to $C001 for a sum of $C003.
        static uint16_t ay(uint16_t ay);

        /// A single byte specifies an address in the first page of memory ($00xx),
        /// also known as the zero page, and the byte at that address is used.
        /// Example: LDY $02 - the value at address $0002 is loaded into Y.
        static uint16_t zp(uint8_t zp);

        /// Zero Page Indexed Indirect: (zp,x)
        /// X is added to the zero page address for a sum address. The little-endian
        /// address stored at that sum address (LSB) and sum+1 (MSB) is used.
        /// Example: STA ($15,X) - $02 in X is added to $15 for a sum of $17; the
        /// address $D010 at $0017/$0018 is where the accumulator gets stored.
        static uint16_t zpx1(int zpx);

        /// Zero Page Indexed with X: zp,x
        /// X is added to the zero page address for a sum address.
        /// Example: LDA $01,X - $02 in X is added to $01 for a sum of $03.
        static uint16_t zpx2(uint8_t zpx);

        /// Zero Page Indirect Indexed with Y: (zp),y
        /// The little-endian address at (zp)/(zp+1) has Y added to it for the final address.
        /// Example: LSR ($2A),Y - $03 in Y is added to the address at $002A/$002B.
        static uint16_t zpy1(uint8_t zpy);

        /// Zero Page Indexed with Y: zp,y
        /// Y is added to the zero page address for a sum address.
        /// Example: LDA $01,Y - $03 in Y is added to $01 for a sum of $04.
        static uint16_t zpy2(uint8_t zpy);

        /// Reads a little-endian 16-bit address from two consecutive memory bytes.
        static uint16_t MemoryValueToAdress(uint16_t a);
    };
}
