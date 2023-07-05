import click

def simulate_desc_alloc(desc_sz, blk_sz, free_streams_space):
    if free_streams_space and free_streams_space[-1] >= desc_sz:
        free_streams_space[-1] -= desc_sz
        return 0
    else:
        # no room in the last stream blk, create a new blk
        # and write how much free space the stream blk has
        free_streams_space.append(blk_sz - desc_sz)
        return 1

import tqdm
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

BLK_SZ = 512

import collections, copy, random
from dataclasses import dataclass

@dataclass
class Action:
    is_delete_action : bool
    obj_id : int

@dataclass
class Obj:
    data_sz : int
    desc_base_sz : int
    obj_id : int
    obj_type : str
    page_no : int
    blk_nr : int
    is_allocd : bool

@dataclass
class Segment:
    blk_cnt: int
    internal_frag : int


class Allocator:
    def __init__(self, *args, **kargs):
        self.internal_frag = 0

    def segment_construction(self, data_sz):
        assert data_sz > 0
        subblock_data_sz = data_sz % BLK_SZ
        blk_cnt = (data_sz // BLK_SZ)
        internal_frag = 0

        if subblock_data_sz:
            internal_frag = BLK_SZ - subblock_data_sz
            blk_cnt += 1 # partially filled blk

        assert blk_cnt >= 0
        return Segment(blk_cnt, internal_frag)

    def chk_subspace(self, obj, segm, space, is_allocd):
        assert obj.is_allocd == is_allocd

        value = obj.obj_id if obj.is_allocd else 0

        startix = obj.blk_nr
        endix = startix + segm.blk_cnt

        if not all(b == value for b in space[startix:endix]):
            import pudb; pudb.set_trace();
        assert all(b == value for b in space[startix:endix])
        assert len(space[startix:endix]) == segm.blk_cnt
        assert all(b != obj.obj_id for b in space[endix:endix+1])
        assert all(b != obj.obj_id for b in space[startix-1:startix])

    def chk_obj_act_consistency_before_alloc(self, obj, act):
        # sanity check: object not alloc'd yet, same id and data size
        # that the action
        assert not obj.is_allocd
        assert obj.obj_id == act.obj_id
        assert not act.is_delete_action

    def chk_obj_and_act_consistency(self, obj, act, is_delete_action):
        # sanity check: object already alloc'd, same id and data size
        # that the action
        assert obj.obj_id == act.obj_id

        if is_delete_action:
            assert act.is_delete_action
            assert obj.is_allocd
            assert obj.blk_nr >= 0
        else:
            assert not act.is_delete_action
            assert not obj.is_allocd

    def create_full_data_blks(self, segm, obj_id):
        # create data blocks with the object id as payload
        # for easy introspection
        return [obj_id] * segm.blk_cnt

    def prealloc_and_track(self, obj):
        segm = self.segment_construction(obj.data_sz)
        self.internal_frag += segm.internal_frag

        blks = self.create_full_data_blks(segm, obj.obj_id)
        return segm, blks

    def predealloc_and_track(self, obj):
        segm = self.segment_construction(obj.data_sz)
        self.internal_frag -= segm.internal_frag

        blks = self.create_full_data_blks(segm, 0)
        assert all(b == 0 for b in blks)
        return segm, blks

    def object_lookup(self, act, is_delete_action):
        obj = self.obj_by_id[act.obj_id]
        assert obj.obj_id == act.obj_id
        assert act.is_delete_action == is_delete_action
        assert (obj.is_allocd and act.is_delete_action) or (not obj.is_allocd and not act.is_delete_action)
        return obj

class MonotonicAllocator(Allocator):
    def __init__(self, space, free_list, obj_by_id):
        Allocator.__init__(self)
        self.space = space
        self.obj_by_id = obj_by_id
        self.internal_frag = 0

    def alloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=False)

        segm, blks = self.prealloc_and_track(obj)

        # just "append" new blocks, the monotonic allocator
        # always grows

        obj.blk_nr = len(self.space)
        obj.is_allocd = True
        self.space.extend(blks)

        self.chk_subspace(obj, segm, self.space, True)
        assert len(self.space) == S + len(blks)

    def dealloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=True)

        segm, zeroed_blks = self.predealloc_and_track(obj)
        self.chk_subspace(obj, segm, self.space, True)

        ix = obj.blk_nr
        endix = ix+segm.blk_cnt

        # dealloc actually does not do anything but for simulation purposes
        # we find the object's block and mark them as freed
        self.space[ix:endix] = zeroed_blks
        obj.is_allocd = False

        self.chk_subspace(obj, segm, self.space, False)
        assert len(self.space) == S


class KRAllocator(Allocator):
    def __init__(self, space, free_list, obj_by_id):
        Allocator.__init__(self)
        self.space = space
        self.free_list = free_list
        self.obj_by_id = obj_by_id
        self.internal_frag = 0

    def alloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=False)

        segm, blks = self.prealloc_and_track(obj)

        # Optimizations: order the list by size so we can
        # discard too-small entries quickly
        for i in range(len(self.free_list)):
            b, cnt = self.free_list[i]
            assert b >= 0 and cnt >=0
            assert len(self.space) >= b+cnt

            if cnt == segm.blk_cnt:
                # best fit, unlink from the free list
                del self.free_list[i]

                self.space[b:b+segm.blk_cnt] = blks
                assert len(self.space) == S
                obj.blk_nr = b
                obj.is_allocd = True

                self.chk_subspace(obj, segm, self.space, True)
                return

            elif cnt > segm.blk_cnt:
                # good enough fit, update in place the free list
                self.free_list[i] = (b+segm.blk_cnt, cnt-segm.blk_cnt)

                self.space[b:b+segm.blk_cnt] = blks
                assert len(self.space) == S
                obj.blk_nr = b
                obj.is_allocd = True

                self.chk_subspace(obj, segm, self.space, True)
                return

        # no fit at all, alloc more space
        obj.blk_nr = len(self.space)
        self.space.extend(blks)
        obj.is_allocd = True

        self.chk_subspace(obj, segm, self.space, True)
        assert len(self.space) == S + len(blks)

    def dealloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=True)

        segm, zeroed_blks = self.predealloc_and_track(obj)
        self.chk_subspace(obj, segm, self.space, True)

        ix = obj.blk_nr
        endix = ix+segm.blk_cnt

        self.space[ix:endix] = zeroed_blks
        obj.is_allocd = False

        if self.coalescing:
            found = 0
            for i in range(len(self.free_list)):
                fr_ix, fr_blk_cnt = self.free_list[i]
                if endix == fr_ix:
                    # <current><next free>
                    self.free_list[i] = (ix, segm.blk_cnt+fr_blk_cnt)
                    found += 1

                elif fr_ix + fr_blk_cnt == ix:
                    # <prev free><current>
                    self.free_list[i] = (fr_ix, fr_blk_cnt+segm.blk_cnt)
                    found += 1

                if found == 2:
                    break

            if found == 0:
                self.free_list.append((ix, segm.blk_cnt))
        else:
            # naively append, we are not doing "coalescing"
            self.free_list.append((ix, segm.blk_cnt))

        self.chk_subspace(obj, segm, self.space, False)
        assert len(self.space) == S

class BuddyAllocator:
    pass

class SegAllocator:
    pass

class PageAllocator:
    pass

def show_space_obj_ids(space, obj_by_id, allocator):
    print("Object IDs map:")
    W = 30
    for i, b in enumerate(space, 1):
        char = f'{b:04x} '
        print(char, end='')
        if (i % W) == 0:
            print() # newline
    print()

def show_space_obj_types(space, obj_by_id, allocator):
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

def show_space_pages(space, obj_by_id, allocator):
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

def show_space_stats(space, obj_by_id, allocator):
    free_blk_cnt = sum(1 if b == 0 else 0 for b in space)
    total_blk_cnt = len(space)

    print("Total blk cnt:", total_blk_cnt)
    print("Total file size:", (total_blk_cnt * BLK_SZ) / 1024, "kb")
    print()

    print("Free blk cnt:", free_blk_cnt)
    print("Free size:", (free_blk_cnt * BLK_SZ) / 1024, "kb")
    print("External frag:", round((free_blk_cnt / total_blk_cnt) * 100, 2), "%")
    print()
    print("Internal frag:", round((allocator.internal_frag / ((total_blk_cnt-free_blk_cnt) * BLK_SZ)) * 100, 2), "%")
    print()

    print("Minimum theoretical total blk cnt:", total_blk_cnt - free_blk_cnt)
    print("Minimum theoretical file size:", ((total_blk_cnt - free_blk_cnt) * BLK_SZ) / 1024, "kb")
    print()

SAMPLE_TARGET = 'ph-01'
#SAMPLE_TARGET = 'dc-03'
#SAMPLE_TARGET = 'uk-02'

_samples = ['au-01', 'dc-01', 'dc-02', 'dc-03', 'dc-04', 'dc-05', 'dc-06',
    'dc-07', 'dw-01', 'et-01', 'et-02', 'et-03', 'et-04', 'fo-01',
    'fo-02', 'fo-03', 'fo-04', 'fo-05', 'fo-06', 'fo-07', 'in-01',
    'in-03', 'in-04', 'in-05', 'in-06', 'in-07', 'in-08', 'in-09',
    'in-10', 'in-11', 'in-12', 'in-13', 'in-14', 'ph-01', 'ph-03',
    'ph-05', 'ph-06', 'pi-01', 'pi-02', 'pi-03', 'pi-04', 'pi-05',
    'pi-06', 'uk-01', 'uk-02', 'L']

_destacated_samples = {
        'L': 'uk-02', # large
        }

@click.command()
@click.option('--seed', default=31416)
@click.option('-w', '--note-taker-back-w', default=12, help='cnt elements to shuffle')
@click.option('--dp', default=0.1*8, help='probability to delete a draw')
@click.option('--idp', default=0.01*8, help='probability to delete an "image" draw')
@click.option('--rf', default=0.25, help='scale blksz to add on reinserts, a random from [-blksz * rf, blksz * rf]')
@click.option('-a', '--alloc', type=click.Choice(['mono', 'kr']), required=True)
@click.option('-s', '--sample', type=click.Choice(_samples), default='ph-01')
@click.option('--coalescing/--no-coalescing', default=False)
@click.option('-m', '--writer-model', type=click.Choice(['copier', 'notetaker', 'editor']), default='editor')
def simulate(seed, note_taker_back_w, dp, idp, rf, alloc, sample, coalescing, writer_model):
    SEED = seed
    NOTE_TAKER_BACK_W = max(4, note_taker_back_w)
    DEL_PROB = min(0.9, max(0, dp))
    DEL_IMG_PROB = min(0.9, max(0, idp))
    REINSERT_CHG_SZ_FACTOR = max(0.001, rf)
    ALLOCATOR = alloc
    SAMPLE_TARGET = _destacated_samples.get(sample, sample)
    COALESCING = coalescing
    WRITER_MODEL = writer_model

    def should_be_deleted(obj, rnd):
        pr = rnd.random()
        th = {
                't': DEL_PROB,
                'x': DEL_PROB,
                'i': DEL_IMG_PROB,
                's': DEL_PROB,
                }[obj.obj_type]
        assert 0 <= th <= 0.9
        return pr < th


    df = pd.read_csv('01-results/xopp-dataset-2023.csv')

    df2 = df[['sample', 'type', 'char_length', 'widths_cnt', 'coords_cnt', 'text', 'page']]
    grouped = df2.groupby('sample')

    max_inline_allowed = 0
    for sample, group in grouped:
        if SAMPLE_TARGET and sample != SAMPLE_TARGET:
            continue

        # Collect all the "draw" actions and objects
        _main_actions = []
        _main_obj_by_id = {}
        obj_id = 0
        for _, row in tqdm.tqdm(group.iterrows()):
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
            obj = Obj(sz, desc_sz, obj_id, row['type'], int(row['page']), -1, False)
            act = Action(False, obj.obj_id)
            _main_obj_by_id[obj_id] = obj
            _main_actions.append(act)

        print("-----------")
        # The draw _main_actions are in file-order so they simulate
        # an user that made those draws in that order without deleting
        # anything.
        rnd = random.Random(SEED)
        copier_actions = copy.deepcopy(_main_actions)

        # Let's shuffle the order of the draws
        # Shuffle within an overlapping range to simulate "locality"
        # of the editions
        # This simulates an user that takes notes on a document from the begin
        # to the end but going back and forward in time to time
        rnd = random.Random(SEED)
        notes_taker_actions = copy.deepcopy(_main_actions)
        W = NOTE_TAKER_BACK_W
        for b in range(0, len(_main_actions), W//2):
            e = b + W
            window = notes_taker_actions[b:e]
            rnd.shuffle(window)
            notes_taker_actions[b:e] = window

        # Delete some draws at random.
        # This simulates an user that edits a document adding things
        # in random order (like notes_taker_actions) but also deleting them
        # and readding them (an object may be deleted and readded more than once)
        # on each readding, its size is changed based on its current size and
        # the blk size such the readding may/may not require more/less blocks
        rnd = random.Random(SEED)
        editor_actions = copy.deepcopy(notes_taker_actions)
        L = len(editor_actions)
        i = 0
        while i < L:
            act = editor_actions[i]

            # skip if it is already deleted
            if act.is_delete_action:
                i += 1
                continue

            obj = _main_obj_by_id[act.obj_id]
            if i < L-2 and should_be_deleted(obj, rnd):
                # Choose 2 indexes *after* the current object index i
                delix = i + rnd.randint(1, L-1-i-1)
                reinsertix = delix + rnd.randint(1, L-1-delix)

                assert i < delix < reinsertix < L

                # Copy the draw and mark it to be deleted
                act = copy.deepcopy(act)
                act.is_delete_action = True

                # Insert the deleted draw *after* the original draw
                editor_actions.insert(delix, act)

                # Take the "deleted" object...
                del_obj = _main_obj_by_id[act.obj_id]

                # Create a new object to be (re)inserted
                # Change its object id and add a little of randomness
                # to its size
                reins_obj = copy.deepcopy(del_obj)
                obj_id += 1
                reins_obj.obj_id = obj_id
                reins_obj.data_sz += max(1, rnd.randint(
                    int(-BLK_SZ * REINSERT_CHG_SZ_FACTOR),
                    int(BLK_SZ * REINSERT_CHG_SZ_FACTOR)
                    ))

                # Add it to the global pool
                _main_obj_by_id[reins_obj.obj_id] = reins_obj

                # Insert a "draw" action *after the deletion, so the total
                # count of draws in the experiment is the same for the rest
                # of scenarios without deletions
                act = copy.deepcopy(act)
                act.is_delete_action = False
                act.obj_id = reins_obj.obj_id

                # insert the "readding" action in some future moment
                editor_actions.insert(reinsertix, act)

                assert (
                            not editor_actions[i].is_delete_action
                        and     editor_actions[delix].is_delete_action
                        and not editor_actions[reinsertix].is_delete_action
                        )
                assert (editor_actions[i].obj_id == editor_actions[delix].obj_id != editor_actions[reinsertix].obj_id)

                # Update L so we process the new actions added too
                L = len(editor_actions)

            i += 1

        print("-----------")
        if WRITER_MODEL == 'copier':
            models_actions = [copier_actions]
        elif WRITER_MODEL == 'notetaker':
            models_actions = [notes_taker_actions]
        elif WRITER_MODEL == 'editor':
            models_actions = [editor_actions]
        else:
            assert False

        for actions in models_actions:
            space = []
            free_list = []
            obj_by_id = copy.deepcopy(_main_obj_by_id)

            if ALLOCATOR == 'mono':
                allocator = MonotonicAllocator(space, free_list, obj_by_id)
            elif ALLOCATOR == 'kr':
                allocator = KRAllocator(space, free_list, obj_by_id)
            else:
                assert False

            allocator.coalescing = COALESCING

            for act in tqdm.tqdm(actions, total=len(actions)):
                if act.is_delete_action:
                    allocator.dealloc(act)
                else:
                    allocator.alloc(act)

            show_space_pages(space, obj_by_id, allocator)
            show_space_stats(space, obj_by_id, allocator)

if __name__ == '__main__':
    import sys
    simulate()
