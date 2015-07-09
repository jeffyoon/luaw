--[[
Copyright (c) 2015 raksoras

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
]]

local constants = require('luaw_constants')
local scheduler = require('luaw_scheduler')

local TS_BLOCKED_EVENT = constants.TS_BLOCKED_EVENT
local DEFAULT_CONNECT_TIMEOUT = constants.DEFAULT_CONNECT_TIMEOUT
local DEFAULT_READ_TIMEOUT = constants.DEFAULT_READ_TIMEOUT
local DEFAULT_WRITE_TIMEOUT = constants.DEFAULT_WRITE_TIMEOUT
local CONN_BUFFER_SIZE = constants.CONN_BUFFER_SIZE

local conn = luaw_tcp_lib.newConnection();
local connMT = getmetatable(conn)
conn:close()

local startReadingInternal = connMT.startReading
local readInternal = connMT.read
--local peekInternal = connMT.peek
--local matchReadInternal = connMT.matchRead
--local searchReadInternal = connMT.searchRead
local writeInternal = connMT.write

connMT.startReading = function(self)
    local status, mesg = startReadingInternal(self)
    assert(status, mesg)
end

function asyncRead(self, buff, readTimeout)
    readTimeout = readTimeout or DEFAULT_READ_TIMEOUT
    local status, mesg = readInternal(self, buff, scheduler.tid(), readTimeout)
    if (status) then
        -- wait for libuv on_read callback
        status, mesg = coroutine.yield(TS_BLOCKED_EVENT)
    end
    return status, mesg
end

connMT.read = asyncRead

connMT.readMinLength = function(self, buff, readTimeout, minReadLen)
    if (buff:capacity() < minReadLen) then
        return false, "Buffer capacity is less than minimum read length requested"
    end

    local status, mesg = true
    while ((buff:length() < minReadLen)and(status)) do
        status, mesg = asyncRead(self, buff, readTimeout)
    end
    return status, mesg
end

--connMT.peek = function(self, peekLen, readTimeout) 
--    local status, err = self:read(peekLen, readTimeout or DEFAULT_READ_TIMEOUT)
--    if (not status) do
--        return status, err
--    end
--    return peekInternal(self, peekLen)
--end

--connMT.matchRead = function(self, matchStr, readTimeout) 
--    local status, err = self:read(#matchStr, readTimeout or DEFAULT_READ_TIMEOUT)
--    if (not status) do
--        return status, err
--    end
--    return matchReadInternal(self, peekLen)
--end

--connMT.searchRead = function(self, searchStr, readTimeout) 
--    if (status) do
--        status, match = matchReadInternal(self, peekLen)
--    end
--    return status, match
--end

--connMT.bufferedSearchRead = function(self, searchStr, readTimeout)
--    local buffer
--    local status, found, part = searchReadInternal(self, searchStr)
--    while ((status)and(not found)) do
--        buffer = safeAppend(buffer, part)
--        status, found, part = searchReadInternal(self)
--    end
--    buffer = safeAppend(buffer, part)
--    return status, found, buffer
--end


connMT.write = function(self, str, writeTimeout)
    local status, nwritten = writeInternal(self, scheduler.tid(), str, writeTimeout  or DEFAULT_WRITE_TIMEOUT)
    if ((status)and(nwritten > 0)) then
        -- there is something to write, yield for libuv callback
        status, nwritten = coroutine.yield(TS_BLOCKED_EVENT)
    end
    assert(status, nwritten)
    return nwritten
end

local connectInternal = luaw_tcp_lib.connect

local function connect(hostIP, hostName, port, connectTimeout)
    assert((hostName or hostIP), "Either hostName or hostIP must be specified in request")
    local threadId = scheduler.tid()
    if not hostIP then
        local status, mesg = luaw_tcp_lib.resolveDNS(hostName, threadId)
        assert(status, mesg)
        status, mesg = coroutine.yield(TS_BLOCKED_EVENT)
        assert(status, mesg)
        hostIP = mesg
    end

    local connectTimeout = connectTimeout or DEFAULT_CONNECT_TIMEOUT
    local conn, mesg = connectInternal(hostIP, port, threadId, connectTimeout)

    -- initial connect_req succeeded, block for libuv callback
    assert(coroutine.yield(TS_BLOCKED_EVENT))
    return conn, mesg
end

luaw_tcp_lib.connect = connect

return luaw_tcp_lib