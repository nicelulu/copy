/**
 * Copyright 2016-2023 ClickHouse, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <string>

#include <IO/ReadBufferFromFileDescriptor.h>
#include <IO/LimitReadBuffer.h>
#include <IO/WriteBufferFromFileDescriptor.h>
#include <IO/copyData.h>
#include <IO/WriteHelpers.h>


int main(int argc, char ** argv)
{
    using namespace RK;

    if (argc < 2)
    {
        std::cerr << "Usage: program limit < in > out\n";
        return 1;
    }

    UInt64 limit = std::stol(argv[1]);

    ReadBufferFromFileDescriptor in(STDIN_FILENO);
    WriteBufferFromFileDescriptor out(STDOUT_FILENO);

    writeCString("--- first ---\n", out);
    {
        LimitReadBuffer limit_in(in, limit, false);
        copyData(limit_in, out);
    }

    writeCString("\n--- second ---\n", out);
    {
        LimitReadBuffer limit_in(in, limit, false);
        copyData(limit_in, out);
    }

    writeCString("\n--- the rest ---\n", out);
    copyData(in, out);

    return 0;
}
