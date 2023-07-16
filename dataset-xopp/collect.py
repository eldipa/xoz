import sys
import os
import gzip
import xml.dom.minidom
import pandas as pd
import numpy as np

def fpath2samplename(fpath):
    return os.path.splitext(os.path.basename(fpath))[0]

samples = set()
for fpath in sys.argv[1:]:
    sample = fpath2samplename(fpath)
    if sample not in samples:
        print(f"Found sample: {sample}")
        samples.add(sample)
    else:
        print(f"Sample '{sample}' duplicated.")
        sys.exit(1)


ATTRS_COLUMNS = [
        # stroke
        "color",
        "fill",
        "capStyle",
        "style",

        # text
        "font",
        "size",

        # stroke & text
        "fn",
        "ts",

        # teximage
        "text",
        ]

def common_attrs(node):
    attrs = []
    for key in ATTRS_COLUMNS:
        try:
            val = str(node.attributes[key].value)
        except KeyError:
            val = ""

        if key in ("size", "ts"):
            if val[-2:] == 'll':
                val = val[:-2]
            val = float(val) if val else 0

        if key in ("fill", ):
            val = int(val) if val else 0

        attrs.append(val)

    return tuple(attrs)


BG_ATTRS_COLUMNS = [
        # all
        "name",

        # solid
        "config",
        "color",
        "style",

        # pixmap & pdf
        "domain",
        "filename",

        # pdf
        "pageno",
        ]

def bg_common_attrs(node):
    attrs = []
    for key in BG_ATTRS_COLUMNS:
        try:
            val = str(node.attributes[key].value)
            if not val:
                # mark as truly empty
                val = '-empty-'
        except KeyError:
            val = ""

        if key in ("pageno", ):
            if val[-2:] == 'll':
                val = val[:-2]
            val = int(val) if val else 0

        attrs.append(val)

    return tuple(attrs)

dfs = []
bg_dfs = []
coords_dfs = []
for fpath in sys.argv[1:]:
    sample = fpath2samplename(fpath)
    if sample not in samples:
        sys.exit(1)

    print(f"Processing sample: {sample}")

    dom = xml.dom.minidom.parse(gzip.open(fpath))
    xournal = dom.childNodes[0]

    pages = [el for el in xournal.childNodes if el.nodeName == 'page']
    print(f"Pages: {len(pages)}")

    layer_cnt = len([el for el in pages[0].childNodes if el.nodeName == 'layer'])
    print(f"Layers: {layer_cnt}")


    data = []
    bg_data = []
    coords_data = []

    unknown = set()

    for page_ix, page in enumerate(pages):
        bg_cnt = 0
        for el in page.childNodes:
            if el.nodeName in ('layer', '#text'):
                continue

            if el.nodeName == 'background':
                bg_cnt += 1
                width = float(page.attributes['width'].value)
                height = float(page.attributes['height'].value)

                attrs = bg_common_attrs(el)
                byte_cnt = len(el.toxml())
                bg_type = el.attributes['type'].value

                bg_data.append(
                        (sample, bg_type, page_ix, width, height) + attrs
                        )
            else:
                unknown.add(el.nodeName)

        assert bg_cnt == 1

        layers = [el for el in page.childNodes if el.nodeName == 'layer']
        for layer_ix, layer in enumerate(layers):
            nontexts = [el for el in layer.childNodes if el.nodeName != '#text']

            stroke_num = 0
            for nontext in nontexts:
                attrs = common_attrs(nontext)
                if nontext.nodeName == 'stroke':
                    byte_cnt = len(nontext.toxml())

                    width_cnt = len(nontext.attributes['width'].value.strip().split())

                    coord_cnt = len(nontext.childNodes[0].data.strip().split())

                    data.append(
                            (sample, 's', page_ix, layer_ix, byte_cnt, width_cnt, coord_cnt) + attrs
                            )


                    coords = [float(c) for c in nontext.childNodes[0].data.strip().split()]

                    x_coords = coords[::2]
                    y_coords = coords[1::2]

                    widths = [float(w) for w in nontext.attributes['width'].value.split()]

                    width = widths[0]
                    if len(widths) == 1:
                        pressures = [0.0] * len(x_coords)

                    else:
                        # add a dummy pressure coord to the end because the count of pressures
                        # is always one minus the count of x and y coords (count of points)
                        pressures = widths[1:] + [float(np.mean(widths[1:]))]

                    del widths

                    if not (len(pressures) == len(x_coords) == len(y_coords)):
                        import pudb; pudb.set_trace();

                    for x, y, p in zip(x_coords, y_coords, pressures):
                        coords_data.append(
                                (sample, page_ix, layer_ix, stroke_num, x, y, width, p)
                                )

                    stroke_num += 1


                elif nontext.nodeName == 'text':
                    text_len = len(nontext.childNodes[0].data.strip())

                    data.append(
                            (sample, 't', page_ix, layer_ix, text_len, 0, 0) + attrs
                            )

                elif nontext.nodeName == 'teximage':
                    teximage_len = len(nontext.childNodes[0].data.strip())

                    data.append(
                            (sample, 'x', page_ix, layer_ix, teximage_len, 0, 0) + attrs
                            )

                elif nontext.nodeName == 'image':
                    img_len = len(nontext.childNodes[0].data.strip())

                    data.append(
                            (sample, 'i', page_ix, layer_ix, img_len, 0, 0) + attrs
                            )

                else:
                    unknown.add(nontext.nodeName)
                    continue

    df = pd.DataFrame(data, columns=['sample', 'type', 'page', 'layer', 'char_length', 'widths_cnt', 'coords_cnt'] + ATTRS_COLUMNS)
    dfs.append(df)

    bg_df = pd.DataFrame(bg_data, columns=['sample', 'type', 'page', 'width', 'height'] + BG_ATTRS_COLUMNS)
    bg_dfs.append(bg_df)

    coords_df = pd.DataFrame(coords_data, columns=['sample', 'page', 'layer', 'stroke', 'x', 'y', 'width', 'p'])
    coords_dfs.append(coords_df)

    if unknown:
        print("Unknown tags:", unknown)

print("Saving...")
df = pd.concat(dfs, ignore_index=True)
df.to_csv("xopp-dataset-2023.csv", index=False)

bg_df = pd.concat(bg_dfs, ignore_index=True)
bg_df.to_csv("bg-xopp-dataset-2023.csv", index=False)

coords_df = pd.concat(coords_dfs, ignore_index=True)
coords_df.to_csv("coords-xopp-dataset-2023.csv", index=False)
