
def simulate_full_blk_alloc(sz, blk_sz):
    full_blk_cnt = sz // blk_sz

    partial_blk_cnt = 1 if sz % blk_sz else 0
    intfrag = blk_sz - sz % blk_sz if partial_blk_cnt else 0

    return full_blk_cnt, intfrag

def simulate_sub_blk_alloc(sz, blk_sz, free_subblks, strategy):
    subblk_sz = (blk_sz // 16)

    remain = sz % blk_sz

    sub_blk_cnt = remain // subblk_sz
    sub_blk_cnt += 1 if remain % subblk_sz else 0

    intfrag = (subblk_sz - (remain % subblk_sz)) if sub_blk_cnt else 0

    newblk = 0
    if sub_blk_cnt:
        # first/best fit strategies
        if strategy == 'first':
            for ix, fb in enumerate(free_subblks):
                if fb >= sub_blk_cnt:
                    free_subblks[ix] -= sub_blk_cnt
                    break
            else:
                # no room in any other blk, create a new blk
                # and write how many free sub blocks have
                free_subblks.append(16 - sub_blk_cnt)
                newblk = 1
        elif strategy == 'best':
            best_ix = -1
            for ix, fb in enumerate(free_subblks):
                if fb >= sub_blk_cnt and free_subblks[best_ix] >= sub_blk_cnt and fb < free_subblks[best_ix]:
                    best_ix = ix

            if best_ix == -1:
                free_subblks.append(16 - sub_blk_cnt)
                newblk = 1
            else:
                free_subblks[best_ix] -= sub_blk_cnt
        else:
            assert False
    else:
        assert remain == 0

    return sub_blk_cnt, intfrag, newblk

def simulate_inline_alloc(sz, blk_sz, max_inline_allowed):
    if sz <= max_inline_allowed:
        return sz
    else:
        return 0

def simulate_desc_alloc(desc_sz, blk_sz, free_streams_space):
    if free_streams_space and free_streams_space[-1] >= desc_sz:
        free_streams_space[-1] -= desc_sz
        return 0
    else:
        # no room in the last stream blk, create a new blk
        # and write how much free space the stream blk has
        free_streams_space.append(blk_sz - desc_sz)
        return 1


import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

df = pd.read_csv('01-results/xopp-dataset-2023.csv')

df2 = df[['sample', 'type', 'char_length', 'widths_cnt', 'coords_cnt', 'text']]
grouped = df2.groupby('sample')

sample_target = None #'ph-01'

ret = []

max_inline_allowed = 0
for blk_sz in [512, 1024, 2048, 4096]:
    for setting in ('full-only', 'subfirst', 'subbest', ):
        for sample, group in grouped:
            if sample_target and sample != sample_target:
                continue

            free_streams_space = []
            free_subblks = []
            tot_blk_cnt = 0
            tot_intfrag = 0
            tot_desc_blk_cnt = 0
            tot_sz = 0
            tot_obj_cnt = 0
            tot_inlined = 0
            tot_inlined_objs = 0
            max_extent = 0
            for _, row in group.iterrows():
                if row['type'] == 's':
                    # strokes: coord and w count * 4 (assuming float)
                    sz = (row['coords_cnt'] + row['widths_cnt']) * 4
                    desc_sz = 22
                elif row['type'] == 'x':
                    # teximage: img decoded + latex source code
                    sz = (row['char_length'] / 1.33) + len(row['text'])
                    desc_sz = 18
                elif row['type'] == 't':
                    # text: text length
                    sz = row['char_length']
                    desc_sz = 26
                elif row['type'] == 'i':
                    # image: img decoded
                    sz = (row['char_length'] / 1.33)
                    desc_sz = 18

                sz = round(sz)
                tot_sz += sz
                row['sz'] = sz
                tot_obj_cnt += 1

                if setting == 'full-only':
                    # try full blk alloc only
                    blk_cnt, intf = simulate_full_blk_alloc(sz, blk_sz)
                    blk_cnt += 1 if intf else 0

                    max_extent = max(blk_cnt, max_extent)

                    desc_blk_cnt = simulate_desc_alloc(desc_sz, blk_sz, free_streams_space)
                    tot_desc_blk_cnt += desc_blk_cnt

                    tot_intfrag += intf
                    tot_blk_cnt += blk_cnt + desc_blk_cnt

                    assert sz <= blk_cnt * blk_sz

                elif setting == 'subfirst':
                    # try full blk alloc plus suballoc
                    blk_cnt, _ = simulate_full_blk_alloc(sz, blk_sz)
                    max_extent = max(blk_cnt, max_extent)

                    sub_blk_cnt, intf, newblk = simulate_sub_blk_alloc(sz, blk_sz, free_subblks, 'first')

                    desc_blk_cnt = simulate_desc_alloc(desc_sz, blk_sz, free_streams_space)
                    tot_desc_blk_cnt += desc_blk_cnt

                    tot_intfrag += intf
                    tot_blk_cnt += blk_cnt + newblk + desc_blk_cnt

                    assert sz <= blk_cnt * blk_sz + sub_blk_cnt * (blk_sz // 16)

                elif setting == 'subbest':
                    # try full blk alloc plus suballoc
                    blk_cnt, _ = simulate_full_blk_alloc(sz, blk_sz)
                    max_extent = max(blk_cnt, max_extent)

                    sub_blk_cnt, intf, newblk = simulate_sub_blk_alloc(sz, blk_sz, free_subblks, 'best')

                    desc_blk_cnt = simulate_desc_alloc(desc_sz, blk_sz, free_streams_space)
                    tot_desc_blk_cnt += desc_blk_cnt

                    tot_intfrag += intf
                    tot_blk_cnt += blk_cnt + newblk + desc_blk_cnt

                    assert sz <= blk_cnt * blk_sz + sub_blk_cnt * (blk_sz // 16)
                else:
                    assert False


            tot_extfrag_subblk = sum(free_subblks) * (blk_sz // 16)

            # do not count for the last of the blocks dedicated to the descriptors
            free_streams_space.pop()
            tot_extfrag_stm = sum(free_streams_space)

            print(f"Sample {sample} ({blk_sz}/{setting}/{max_inline_allowed}):")
            print(f"User data size:\t\t\t {tot_sz} bytes")
            print(f"File size:\t\t\t {tot_blk_cnt * blk_sz} bytes\t\t ({(tot_blk_cnt * blk_sz)/tot_sz:.05} tot blk bytes/user bytes)")
            print(f"Object count:\t\t\t {tot_obj_cnt}")
            print(f"Block count (data + desc):\t {tot_blk_cnt} (#data + #desc)")
            print(f"Block count (desc):\t\t {tot_desc_blk_cnt} #desc\t\t ({tot_desc_blk_cnt / tot_blk_cnt:.05} #desc/#data+#desc)")
            print(f"Max extent length:\t\t {max_extent} #blk")
            print(f"Inlined data size:\t\t {tot_inlined} bytes\t\t ({tot_inlined / tot_sz:.05} inlined/user bytes)")
            print(f"Inlined objects:\t\t {tot_inlined_objs} #obj\t\t ({tot_inlined_objs / tot_obj_cnt:.05} inlined/#objs)")
            print(f"Internal Frag:\t\t\t {tot_intfrag} bytes\t\t ({tot_intfrag / tot_sz:.05} frag bytes/user bytes)")
            print(f"External Frag (subblk):\t\t {tot_extfrag_subblk} bytes\t\t ({tot_extfrag_subblk / tot_sz:.05} frag bytes/user bytes)")
            print(f"External Frag (desc):\t\t {tot_extfrag_stm} bytes\t\t ({tot_extfrag_stm / tot_sz:.05} frag bytes/user bytes)")
            print()

            ret.append(
                    [sample, blk_sz, setting, max_inline_allowed,
                     tot_sz,
                     tot_blk_cnt * blk_sz,
                     tot_obj_cnt,
                     tot_blk_cnt,
                     tot_desc_blk_cnt,
                     max_extent,
                     tot_inlined,
                     tot_inlined_objs,
                     tot_intfrag,
                     tot_extfrag_subblk,
                     tot_extfrag_stm
                     ])

for blk_sz in [512, 1024, 2048, 4096]:
    for max_inline_allowed in [32, 64, 128, 256, 512]:
        setting = 'inline-subfirst'

        for sample, group in grouped:
            if sample_target and sample != sample_target:
                continue

            free_streams_space = []
            free_subblks = []
            tot_blk_cnt = 0
            tot_intfrag = 0
            tot_desc_blk_cnt = 0
            tot_sz = 0
            tot_obj_cnt = 0
            tot_inlined = 0
            tot_inlined_objs = 0
            max_extent = 0
            for _, row in group.iterrows():
                if row['type'] == 's':
                    # strokes: coord and w count * 4 (assuming float)
                    sz = (row['coords_cnt'] + row['widths_cnt']) * 4
                    desc_sz = 22
                elif row['type'] == 'x':
                    # teximage: img decoded + latex source code
                    sz = (row['char_length'] / 1.33) + len(row['text'])
                    desc_sz = 18
                elif row['type'] == 't':
                    # text: text length
                    sz = row['char_length']
                    desc_sz = 26
                elif row['type'] == 'i':
                    # image: img decoded
                    sz = (row['char_length'] / 1.33)
                    desc_sz = 18

                sz = round(sz)
                tot_sz += sz
                row['sz'] = sz
                tot_obj_cnt += 1

                if setting == 'inline-subfirst':
                    # try full blk alloc plus inline first, suballoc as fallback
                    inlined_data = simulate_inline_alloc(sz, blk_sz, max_inline_allowed)
                    desc_sz += inlined_data

                    if inlined_data:
                        tot_inlined += inlined_data
                        tot_inlined_objs += 1
                        blk_cnt = newblk = intf = 0
                    else:
                        blk_cnt, _ = simulate_full_blk_alloc(sz, blk_sz)
                        max_extent = max(blk_cnt, max_extent)

                        sub_blk_cnt, intf, newblk = simulate_sub_blk_alloc(sz, blk_sz, free_subblks, 'first')

                    desc_blk_cnt = simulate_desc_alloc(desc_sz, blk_sz, free_streams_space)
                    tot_desc_blk_cnt += desc_blk_cnt

                    tot_intfrag += intf
                    tot_blk_cnt += blk_cnt + newblk + desc_blk_cnt

                    if inlined_data:
                        assert 0 == (blk_cnt + newblk) * blk_sz
                        assert sz == inlined_data
                    else:
                        assert sz <= blk_cnt * blk_sz + sub_blk_cnt * (blk_sz // 16)
                else:
                    assert False


            tot_extfrag_subblk = sum(free_subblks) * (blk_sz // 16)

            # do not count for the last of the blocks dedicated to the descriptors
            free_streams_space.pop()
            tot_extfrag_stm = sum(free_streams_space)

            print(f"Sample {sample} ({blk_sz}/{setting}/{max_inline_allowed}):")
            print(f"User data size:\t\t\t {tot_sz} bytes")
            print(f"File size:\t\t\t {tot_blk_cnt * blk_sz} bytes\t\t ({(tot_blk_cnt * blk_sz)/tot_sz:.05} tot blk bytes/user bytes)")
            print(f"Object count:\t\t\t {tot_obj_cnt}")
            print(f"Block count (data + desc):\t {tot_blk_cnt} (#data + #desc)")
            print(f"Block count (desc):\t\t {tot_desc_blk_cnt} #desc\t\t ({tot_desc_blk_cnt / tot_blk_cnt:.05} #desc/#data+#desc)")
            print(f"Max extent length:\t\t {max_extent} #blk")
            print(f"Inlined data size:\t\t {tot_inlined} bytes\t\t ({tot_inlined / tot_sz:.05} inlined/user bytes)")
            print(f"Inlined objects:\t\t {tot_inlined_objs} #obj\t\t ({tot_inlined_objs / tot_obj_cnt:.05} inlined/#objs)")
            print(f"Internal Frag:\t\t\t {tot_intfrag} bytes\t\t ({tot_intfrag / tot_sz:.05} frag bytes/user bytes)")
            print(f"External Frag (subblk):\t\t {tot_extfrag_subblk} bytes\t\t ({tot_extfrag_subblk / tot_sz:.05} frag bytes/user bytes)")
            print(f"External Frag (desc):\t\t {tot_extfrag_stm} bytes\t\t ({tot_extfrag_stm / tot_sz:.05} frag bytes/user bytes)")
            print()

            ret.append(
                    [sample, blk_sz, setting, max_inline_allowed,
                     tot_sz,
                     tot_blk_cnt * blk_sz,
                     tot_obj_cnt,
                     tot_blk_cnt,
                     tot_desc_blk_cnt,
                     max_extent,
                     tot_inlined,
                     tot_inlined_objs,
                     tot_intfrag,
                     tot_extfrag_subblk,
                     tot_extfrag_stm
                     ])

print("Saving...")
ret_df = pd.DataFrame(ret, columns=[
    'sample', 'blk_sz', 'setting', 'max_inline_allowed',
    'tot_user_sz',
    'tot_blk_sz',
    'tot_obj_cnt',
    'tot_blk_cnt',
    'tot_desc_blk_cnt',
    'max_extent',
    'tot_inlined',
    'tot_inlined_objs',
    'tot_intfrag',
    'tot_extfrag_subblk',
    'tot_extfrag_stm',
    ])

#ret_df.to_csv("xopp-dataset-2023-frag-stats.csv", index=False)

