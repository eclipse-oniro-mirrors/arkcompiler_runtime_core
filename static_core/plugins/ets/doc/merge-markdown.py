#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright (c) 2021-2023 Huawei Device Co., Ltd.
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import markdown
import os
import re
import sys

def get_chapters(source_dir, target, build_dir):
    names = []
    with open(os.path.join(source_dir, target, "index.rst")) as ind:
        for line in ind:
            line = line.strip()
            if line.startswith("/"):
                names.append(line[1:])

    result = []
    for name in names:
        result.append(os.path.join(build_dir, target + "-md", name + ".md"))

    return result

def merge_chapters(chapters, dest_path):
    with open(dest_path, "w") as dest_file:
        for chapter in chapters:
            with open(chapter, "r") as chapter_file:
                for line in chapter_file:
                    # Hacks to mitigate Markdown builder bugs
                    line = line.replace("`a | b`", "`a \| b`")
                    line = line.replace("`a || b`", "`a \|\| b`")

                    # Hacks to avoid recipes subheaders
                    if (line.startswith("### Rule")):
                        line = line.strip().replace("### Rule", "**Rule") + "**\n"
                    if (line == "### TypeScript\n"):
                        line = "**TypeScript**\n"
                    if (line == "### ArkTS\n"):
                        line = "**ArkTS**\n"
                    if (line == "### See also\n"):
                        line = "**See also**\n"

                    # A hack to cut off hyperlinks
                    if (line.startswith("<a id=")):
                        next(chapter_file)
                        continue

                    # A hack to cut off keyword comments
                    if (line.startswith("<!--")):
                        next(chapter_file)
                        next(chapter_file)
                        continue

                    # A hack to make recipe links a simple text
                    line = re.sub("\[(.+)\]\(#r[0-9]+\)", "\\1", line)

                    # Fix links to recipes
                    line = line.replace("(recipes.md)", "(#recipes)")

                    # Fix link to quick-start dir
                    line = line.replace("https://gitee.com/openharmony/docs/blob/master/en/application-dev/quick-start/", "../")

                    dest_file.write(line)

            dest_file.write("\n")

def main():
    if len(sys.argv) != 4:
        sys.exit("Usage:\n" + sys.argv[0] + "source_dir target build_dir")

    source_dir = sys.argv[1]
    target = sys.argv[2]
    build_dir = sys.argv[3]

    chapters = get_chapters(source_dir, target, build_dir)

    dest_file = os.path.join(build_dir, target + ".md")
    merge_chapters(chapters, dest_file)

if __name__ == "__main__":
    main()
