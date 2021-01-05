--- script for generating crossfade curves for wrIHead
-- heavily based on prior work by emb for softcut

-- run from this directory: lua delay_fades.lua delay_fades.h delay_fades.c

-- USER MODIFIABLE CONSTANTS
local REC_FADE_DELAY = (1/128)
--local REC_FADE_DELAY = 0
local PRE_FADE_WINDOW = (1/8)
local SIZE = 512
local TYPE = 'float'

-- boilerplate
local hheader =
[[// THIS FILE IS AUTOGENERATED //
// DO NOT EDIT THIS MANUALLY //

#pragma once

]]

local cheader =
[[// THIS FILE IS AUTOGENERATED //
// DO NOT EDIT THIS MANUALLY //

#include "delay_fades.h"

]]


local SIZE_1 = SIZE-1

function make_h_table( _type, name, size, generator)
    return _type .. ' ' .. name .. '_LUT_get( ' .. _type .. ' phase );\n'
end

function make_c_table( _type, name, size, generator)
    local s = 'const ' .. _type .. ' '.. name .. '_LUT[' .. size .. ']={'
    local content = build_LUT(generator)
    for _,v in ipairs(content) do
        s = s .. v .. ',' -- TODO remove leading \n after debug
    end
    s = s:sub(1,-2) -- remove trailing comma
    return s .. '};\n'
             .. _type .. ' ' .. name .. '_LUT_get( ' .. _type .. ' phase ){\n'
             .. '\treturn ' .. name .. '_LUT[(int)(phase * ' .. size-1 .. ')];\n'
             .. '}\n\n'

end

function build_LUT( generator )
    local t = {}
    for i=0,SIZE_1 do
        table.insert(t, generator(i/SIZE_1))
    end
    return t
end


-- calculated single location at phase [0,1]
function pre_fade( phase )
    -- return 1.0
    return math.min( phase / PRE_FADE_WINDOW, 1.0 )
end

function rec_fade( phase )
    if phase < REC_FADE_DELAY then
        return 0.0
    else
        phase = phase * (1 + REC_FADE_DELAY) -- normalize range to [n,n+1]
        phase = phase - REC_FADE_DELAY       -- normalize origin to [0,1]
        phase = -math.cos(phase * math.pi)   -- raised cosine [-1,1]
        phase = phase / 2.0 + 0.5            -- normalize cosine [0,1]
        return math.sqrt(phase)              -- sqrt for equal-power
    end
end

function equal_power( phase )
    return math.sqrt(phase)
end


-- build the output file
function make_tables(f)
    local hstring = hheader
                 .. make_h_table( TYPE ,'pre_fade'   ,SIZE ,pre_fade    )
                 .. make_h_table( TYPE ,'rec_fade'   ,SIZE ,rec_fade    )
                 .. make_h_table( TYPE ,'equal_power',SIZE ,equal_power )
    local cstring = cheader
                 .. make_c_table( TYPE ,'pre_fade'   ,SIZE ,pre_fade    )
                 .. make_c_table( TYPE ,'rec_fade'   ,SIZE ,rec_fade    )
                 .. make_c_table( TYPE ,'equal_power',SIZE ,equal_power )
    return hstring, cstring
end


-- open output file
local out_h_file = arg[1]
local out_c_file = arg[2]
do
    local h, c = make_tables()
    oh = io.open( out_h_file, 'w' )
    oh:write( h )
    oh:close()
    oc = io.open( out_c_file, 'w' )
    oc:write( c )
    oc:close()
end
