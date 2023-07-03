

def simulate_desc_alloc(desc_sz, blk_sz, free_streams_space):
    if free_streams_space and free_streams_space[-1] >= desc_sz:
        free_streams_space[-1] -= desc_sz
        return 0
    else:
        # no room in the last stream blk, create a new blk
        # and write how much free space the stream blk has
        free_streams_space.append(blk_sz - desc_sz)
        return 1

def should_be_deleted(act, delete_factor, rnd):
    pr = rnd.random()
    return pr < min({
            't': 0.10 * delete_factor,
            'x': 0.10 * delete_factor,
            'i': 0.01 * delete_factor,
            's': 0.10 * delete_factor,
            }[act.obj_type], 0.7)

import tqdm
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

df = pd.read_csv('01-results/xopp-dataset-2023.csv')

df2 = df[['sample', 'type', 'char_length', 'widths_cnt', 'coords_cnt', 'text', 'page']]
grouped = df2.groupby('sample')

sample_target = 'ph-01'
#sample_target = 'dc-03'
#sample_target = 'uk-02'

BLK_SZ = 512

import collections, copy, random
from dataclasses import dataclass

@dataclass
class Action:
    is_deleted : bool
    data_sz : int
    blk_cnt : int
    desc_base_sz : int
    obj_id : int
    obj_type : str
    page_no : int

@dataclass
class Obj:
    data_sz : int
    blk_cnt : int
    desc_base_sz : int
    obj_id : int
    obj_type : str
    page_no : int
    blk_nr : int

class MonotonicAllocator:
    def __init__(self, space, free_list, obj_by_id):
        self.space = space
        self.obj_by_id = obj_by_id

    def alloc(self, act):
        # just "append" new blocks, the monotonic allocator
        # always grows
        S = len(self.space)
        obj = self.obj_by_id[act.obj_id]

        assert obj.blk_nr == 0

        blks = [act.obj_id] * act.blk_cnt

        obj.blk_nr = len(self.space)
        self.space.extend(blks)

        assert len(self.space) == S + len(blks)

    def dealloc(self, act):
        # dealloc actually does not do anything but for simulation purposes
        # we find the object's block and mark them as freed
        S = len(self.space)
        obj = self.obj_by_id[act.obj_id]

        ix = obj.blk_nr
        endix = ix+act.blk_cnt

        assert ix >= 0
        assert all(b == act.obj_id for b in self.space[ix:endix])
        assert all(b != act.obj_id for b in self.space[endix:endix+1])

        self.space[ix:endix] = [0] * act.blk_cnt
        obj.blk_nr = 0

        assert len(self.space) == S
        #assert act.obj_id not in self.space

class KRAllocator:
    def __init__(self, space, free_list, obj_by_id):
        self.space = space
        self.free_list = free_list
        self.obj_by_id = obj_by_id

    def alloc(self, act):
        blks = [act.obj_id] * act.blk_cnt
        obj = self.obj_by_id[act.obj_id]

        assert obj.blk_nr == 0

        # Optimizations: order the list by size so we can
        # discard too-small entries quickly
        S = len(self.space)
        for i in range(len(self.free_list)):
            b, cnt = self.free_list[i]
            assert b >= 0 and cnt >=0
            assert len(self.space) >= b+cnt
            if cnt == act.blk_cnt:
                # best fit, unlink from the free list
                del self.free_list[i]
                self.space[b:b+act.blk_cnt] = blks
                assert len(self.space) == S
                obj.blk_nr = b
                return
            elif cnt > act.blk_cnt:
                # good enough fit, update in place the free list
                self.free_list[i] = (b+act.blk_cnt, cnt-act.blk_cnt)
                self.space[b:b+act.blk_cnt] = blks
                assert len(self.space) == S
                obj.blk_nr = b
                return

        # no fit at all, alloc more space
        obj.blk_nr = len(self.space)
        self.space.extend(blks)

        assert len(self.space) == S + len(blks)

    def dealloc(self, act):
        S = len(self.space)
        obj = self.obj_by_id[act.obj_id]

        ix = obj.blk_nr
        endix = ix+act.blk_cnt

        assert ix >= 0
        assert all(b == act.obj_id for b in self.space[ix:endix])
        assert all(b != act.obj_id for b in self.space[endix:endix+1])

        self.space[ix:endix] = [0] * act.blk_cnt
        obj.blk_nr = 0

        # naively append, we are not doing "coalescing"
        self.free_list.append((ix, act.blk_cnt))

        assert len(self.space) == S

class BuddyAllocator:
    pass

class SegAllocator:
    pass

class PageAllocator:
    pass

def show_space_obj_ids(space, obj_by_id):
    print("Object IDs map:")
    W = 30
    for i, b in enumerate(space, 1):
        char = f'{b:04x} '
        print(char, end='')
        if (i % W) == 0:
            print() # newline
    print()

def show_space_obj_types(space, obj_by_id):
    print("Object types map:")
    W = 60
    for i, b in enumerate(space, 1):
        if b:
            obj = obj_by_id[b]
            char = obj.obj_type * 2
        else:
            char = '..'
        print(char + ' ', end='')
        if (i % W) == 0:
            print() # newline
    print()

def show_space_pages(space, obj_by_id):
    print("Pages map:")
    W = 30
    for i, b in enumerate(space, 1):
        if b:
            obj = obj_by_id[b]
            char = f'{obj.page_no:04x} '
        else:
            char = '.... '
        print(char, end='')
        if (i % W) == 0:
            print() # newline

    print()

def show_space_stats(space, obj_by_id):
    free_blk_cnt = sum(1 if b == 0 else 0 for b in space)
    print("Total blk cnt:", len(space))
    print("Total file size:", (len(space) * BLK_SZ) / 1024, "kb")
    print()

    print("Free blk cnt:", free_blk_cnt)
    print("Free size:", (free_blk_cnt * BLK_SZ) / 1024, "kb")
    print("External frag:", round((free_blk_cnt / len(space)) * 100, 2), "%")
    print()

    print("Minimum theoretical total blk cnt:", len(space) - free_blk_cnt)
    print("Minimum theoretical file size:", ((len(space) - free_blk_cnt) * BLK_SZ) / 1024, "kb")
    print()

max_inline_allowed = 0
for sample, group in grouped:
    if sample_target and sample != sample_target:
        continue

    # Collect all the "draw" actions and objects
    _main_actions = []
    _main_obj_by_id = {}
    obj_id = 0
    for _, row in group.iterrows():
        obj_id += 1

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
        blk_cnt = (sz // BLK_SZ) + (1 if sz % BLK_SZ else 0)
        act = Action(False, sz, blk_cnt, desc_sz, obj_id, row['type'], int(row['page']))
        obj = Obj(sz, blk_cnt, desc_sz, obj_id, row['type'], int(row['page']), 0)
        _main_obj_by_id[obj_id] = obj
        _main_actions.append(act)

    print("-----------")
    # The draw _main_actions are in file-order so they simulate
    # an user that made those draws in that order without deleting
    # anything.
    rnd = random.Random(31416)
    copier_actions = copy.deepcopy(_main_actions)

    # Let's shuffle the order of the draws
    # Shuffle within an overlapping range to simulate "locality"
    # of the editions
    # This simulates an user that takes notes on a document from the begin
    # to the end but going back and forward in time to time
    rnd = random.Random(31416)
    notes_taker_actions = copy.deepcopy(_main_actions)
    W = 12
    for b in range(0, len(_main_actions), W//2):
        e = b + W
        window = notes_taker_actions[b:e]
        rnd.shuffle(window)
        notes_taker_actions[b:e] = window

    # Delete some draws at random.
    # This simulates an user that edits a document adding things
    # in random order (like notes_taker_actions) but also deleting them
    # and readding them (an object may be deleted and readded more than once)
    rnd = random.Random(31416)
    editor_actions = copy.deepcopy(notes_taker_actions)
    L = len(editor_actions)
    i = 0
    while i < L:
        act = editor_actions[i]

        # skip if it is already deleted
        if act.is_deleted:
            i += 1
            continue

        if should_be_deleted(act, 8, rnd):
            # Copy the draw and mark it to be deleted
            act = copy.deepcopy(act)
            act.is_deleted = True

            # Insert the deleted draw *after* the original draw, at
            # some random position
            delix = i + rnd.randint(1, max(L-i-1, 1))
            editor_actions.insert(delix, act)
            L = len(editor_actions)

            # Insert a "draw" action *after the deletion, so the total
            # count of draws in the experiment is the same for the rest
            # of scenarios without deletions
            act = copy.deepcopy(act)
            act.is_deleted = False
            reinsertix = delix + rnd.randint(1, max(L-delix-1, 1))
            editor_actions.insert(reinsertix, act)
            L = len(editor_actions)

            assert i < delix < reinsertix
            assert (
                    not editor_actions[i].is_deleted
                    and editor_actions[delix].is_deleted
                    and not editor_actions[reinsertix].is_deleted
                    )
            assert (editor_actions[i].obj_id == editor_actions[delix].obj_id == editor_actions[reinsertix].obj_id)

        i += 1

    _main_actions

    print("-----------")
    for actions in [editor_actions]:
        space = []
        free_list = []
        obj_by_id = copy.deepcopy(_main_obj_by_id)

        #allocator = MonotonicAllocator(space, free_list, obj_by_id)
        allocator = KRAllocator(space, free_list, obj_by_id)
        for act in tqdm.tqdm(actions, total=len(actions)):
            if act.is_deleted:
                allocator.dealloc(act)
            else:
                allocator.alloc(act)

        show_space_pages(space, obj_by_id)
        show_space_stats(space, obj_by_id)
