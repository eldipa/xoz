import sys
import os
import gzip

try:
    fpath = sys.argv[1]
    template = sys.argv[2]
    batch = int(sys.argv[3])
    assert batch > 0
except (IndexError, ValueError) as err:
    print("Bad arguments:", err)
    print("Usage: spli_xopp.py <source xopp> <prefix> <batch>")
    print("Example: spli_xopp.py foo.xopp bar 10")
    print("  -> generates bar_001.xopp bar_002.xopp ... with 10+1 pages each")
    sys.exit(1)

print("Counting pages...")
page_cnt = 0
for line in gzip.open(fpath):
    ln = line.lstrip()
    if ln.startswith(b"<page "):
        page_cnt += 1
print(f"{fpath} has {page_cnt} pages")

page_range_begin = 1
page_range_end = page_range_begin + batch
batch_num = 1

page_written = True
while page_range_begin <= page_cnt:

    fdst = template + f'_{batch_num:03}.xopp'

    print(f"Writing pages {page_range_begin} to {page_range_end} to {fdst}")
    with gzip.open(fpath) as src, gzip.open(fdst, 'wb') as out:
        page_no = 0

        inside_rejected_page = False
        for line in src:
            ln = line.lstrip()
            if ln.startswith(b"<page "):
                page_no += 1
                if page_no == 1 or (page_range_begin <= page_no < page_range_end):
                    out.write(line)
                    inside_rejected_page = False
                    continue
                else:
                    inside_rejected_page = True
                    continue

            elif ln.startswith(b"</page>"):
                if inside_rejected_page:
                    inside_rejected_page = False
                    continue
                else:
                    out.write(line)
                    inside_rejected_page = False
                    continue

            if inside_rejected_page:
                continue
            else:
                out.write(line)

    page_range_begin = page_range_end
    page_range_end += batch
    batch_num += 1
