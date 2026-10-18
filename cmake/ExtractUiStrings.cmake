# Collects every qsTr("...") / tr("...") string from the given sources into one file,
# one string per line (still backslash-escaped as in the source), sorted and unique.
# The app embeds the result so Settings can write a complete translation template.
#
# Usage: cmake -DOUT=<file> -DSOURCE_LIST=<file listing sources, one per line> -P ExtractUiStrings.cmake

file(STRINGS "${SOURCE_LIST}" sources)
set(found "")
foreach(source IN LISTS sources)
    file(READ "${source}" content)
    # ';' separates CMake list items: park it as a placeholder while matching, restore on write.
    string(REPLACE ";" "@SEMICOLON@" content "${content}")
    string(REGEX MATCHALL "(qsTr|[^A-Za-z_]tr)\\(\"([^\"\\\\]|\\\\.)*\"" matches "${content}")
    foreach(match IN LISTS matches)
        string(REGEX REPLACE "^.*\\(\"(.*)\"$" "\\1" text "${match}")
        if(NOT text STREQUAL "")
            list(APPEND found "${text}")
        endif()
    endforeach()
endforeach()
list(REMOVE_DUPLICATES found)
list(SORT found)
list(JOIN found "\n" text)
string(REPLACE "@SEMICOLON@" ";" text "${text}")
file(WRITE "${OUT}.tmp" "${text}\n")
# Only touch the output when it changed, so the resource is not rebuilt for nothing.
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")
