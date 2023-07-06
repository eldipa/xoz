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

import collections, copy, random, itertools
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
    segm : None

@dataclass
class Segment:
    blk_nr : int
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
        return Segment(None, blk_cnt, internal_frag)

    def chk_subspace(self, obj, segm, space, is_already_allocd):
        ''' Check that the segment associated with the object
            is within the boundaries of the space.

            If is_already_allocd, the object is expected to be alloc'd
            and the data in the space filled with its obj id.

            Otherwise, the object is expected to be not-alloc'd
            and the data in the space filled with zeros
            meaning that the space is not alloc'd by anyone.
        '''
        if is_already_allocd:
            value = obj.obj_id

            # Expected to have a segment because it is already
            # allocated
            assert obj.segm is not None
            assert segm.blk_nr is None or obj.segm.blk_nr == segm.blk_nr
            assert obj.segm.blk_nr >= 0
            assert obj.segm.blk_cnt == segm.blk_cnt
        else:
            value = 0
            assert obj.segm is None

        startix = segm.blk_nr
        endix = startix + segm.blk_cnt

        # Ensure that the blks are within the space boundaries
        assert len(space[startix:endix]) == segm.blk_cnt

        # Ensure that the blks in the space selected are
        # either full with the obj_id (is_already_allocd = True)
        # or with zeros (is_already_allocd = False)
        assert all(b == value for b in space[startix:endix])

        # As a sanity check the previous blk and the next blk
        # outside the selected blocks should *not* be written
        # with the object id (otherwise it would mean that there
        # was an overflow/underflow)
        assert all(b != obj.obj_id for b in space[endix:endix+1])
        assert all(b != obj.obj_id for b in space[startix-1:startix])

    def chk_obj_and_act_consistency(self, obj, act, is_delete_action):
        # sanity check: object already alloc'd and same id
        # that the action
        assert obj.obj_id == act.obj_id

        if is_delete_action:
            assert act.is_delete_action
            assert obj.segm is not None
            assert obj.segm.blk_cnt > 0
            assert obj.segm.blk_nr >= 0
        else:
            assert not act.is_delete_action
            assert obj.segm is None

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
        segm = obj.segm
        self.internal_frag -= segm.internal_frag

        blks = self.create_full_data_blks(segm, 0)
        assert all(b == 0 for b in blks)
        return segm, blks

    def object_lookup(self, act, is_delete_action):
        obj = self.obj_by_id[act.obj_id]
        assert obj.obj_id == act.obj_id

        if is_delete_action:
            assert act.is_delete_action
            assert obj.segm is not None
            assert obj.segm.blk_cnt > 0
        else:
            assert not act.is_delete_action
            assert obj.segm is None

        return obj

    def trace(self, alloc, obj, tag=''):
        if self.trace_enabled:
            print(
                "A" if alloc else "D",
                f"cnt: {obj.segm.blk_cnt: 6}",
                f"start: {obj.segm.blk_nr:06x}",
                f"end: {obj.segm.blk_nr + obj.segm.blk_cnt:06x}",
                "tag:", tag
                )

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

        segm.blk_nr = len(self.space)
        obj.segm = segm
        self.space.extend(blks)
        self.trace(alloc=True, obj=obj, tag='extend')

        self.chk_subspace(obj, segm, self.space, is_already_allocd=True)
        assert len(self.space) == S + len(blks)

    def dealloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=True)

        segm, zeroed_blks = self.predealloc_and_track(obj)
        self.chk_subspace(obj, segm, self.space, is_already_allocd=True)

        ix = obj.segm.blk_nr
        endix = ix+segm.blk_cnt

        # dealloc actually does not do anything but for simulation purposes
        # we find the object's block and mark them as freed
        self.space[ix:endix] = zeroed_blks
        self.trace(alloc=False, obj=obj)

        obj.segm = None

        self.chk_subspace(obj, segm, self.space, is_already_allocd=False)
        assert len(self.space) == S

    def shrink(self):
        pass

class KRAllocator(Allocator):
    def __init__(self, space, free_list, obj_by_id):
        Allocator.__init__(self)
        self.space = space
        self.free_list = free_list
        self.obj_by_id = obj_by_id
        self.internal_frag = 0
        self.global_endix = 0

    def alloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=False)

        segm, blks = self.prealloc_and_track(obj)

        # Optimizations: order the list by size so we can
        # discard too-small entries quickly
        for i in range(len(self.free_list)):
            fr_ix, fr_cnt = self.free_list[i]

            # Sanity check: the free blocks are within
            # the space boundaries
            assert fr_ix >= 0 and fr_cnt > 0
            assert len(self.space) >= fr_ix+fr_cnt

            if fr_cnt == segm.blk_cnt:
                # best fit, unlink from the free list
                del self.free_list[i]

                # store the data
                self.space[fr_ix : fr_ix+segm.blk_cnt] = blks
                assert len(self.space) == S

                # Track where it was alloc'd
                segm.blk_nr = fr_ix
                obj.segm = segm
                self.trace(alloc=True, obj=obj, tag='perfect')

                self.chk_subspace(obj, segm, self.space, is_already_allocd=True)
                return

            elif fr_cnt > segm.blk_cnt:
                # good enough fit, update in place the free list
                self.free_list[i] = (fr_ix + segm.blk_cnt, fr_cnt - segm.blk_cnt)

                self.space[fr_ix : fr_ix+segm.blk_cnt] = blks
                assert len(self.space) == S

                segm.blk_nr = fr_ix
                obj.segm = segm
                self.trace(alloc=True, obj=obj, tag=f'split -> new free cnt:{self.free_list[i][1]: 6} start: {self.free_list[i][0]:06x} end: {self.free_list[i][0]+self.free_list[i][1]:06x}')

                self.chk_subspace(obj, segm, self.space, is_already_allocd=True)
                return

        # no fit at all, alloc more space
        segm.blk_nr = len(self.space)
        obj.segm = segm
        self.global_endix = max(segm.blk_nr + segm.blk_cnt, self.global_endix)
        self.space.extend(blks)
        self.trace(alloc=True, obj=obj, tag='extend')

        self.chk_subspace(obj, segm, self.space, is_already_allocd=True)
        assert len(self.space) == S + len(blks)

    def dealloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=True)

        segm, zeroed_blks = self.predealloc_and_track(obj)
        self.chk_subspace(obj, segm, self.space, is_already_allocd=True)

        startix = obj.segm.blk_nr
        endix = startix+segm.blk_cnt

        self.space[startix:endix] = zeroed_blks

        if self.coalescing:
            found = []
            coalesced = None

            self.free_list = list(sorted(self.free_list))
            for i in range(len(self.free_list)):
                fr_ix, fr_blk_cnt = self.free_list[i]

                if fr_ix + fr_blk_cnt == startix:
                    # fr_ix   startix
                    # v       v
                    # ^.......^....
                    #  fr_cnt   segm.blk_cnt
                    found.append((fr_ix, fr_blk_cnt))
                    coalesced = self.free_list[i] = (fr_ix, fr_blk_cnt+segm.blk_cnt)

                elif endix == fr_ix:
                    #         startix    fr_ix
                    #         v          v
                    #         ^..........^.....
                    #       segm.blk_cnt |   fr_cnt
                    #                  endix
                    found.append((fr_ix, fr_blk_cnt))
                    if coalesced:
                        prev_fr_ix, prev_fr_blk_cnt = self.free_list[i-1]
                        coalesced = self.free_list[i-1] = (prev_fr_ix, prev_fr_blk_cnt+fr_blk_cnt)
                        del self.free_list[i]
                    else:
                        coalesced = self.free_list[i] = (startix, segm.blk_cnt+fr_blk_cnt)

                    break

                elif fr_ix > endix:
                    break

            if not coalesced:
                self.trace(alloc=False, obj=obj, tag=f'new free -> cnt:{segm.blk_cnt: 6} start: {startix:06x} end: {startix+segm.blk_cnt:06x}')
                self.free_list.append((startix, segm.blk_cnt))
            else:
                msg1 = f'cnt:{coalesced[1]: 6} start: {coalesced[0]:06x} end: {coalesced[0]+coalesced[1]:06x}'
                msg2 = '; '.join(f'cnt:{f[1]: 6} start: {f[0]:06x} end: {f[0]+f[1]:06x}' for f in found)
                self.trace(alloc=False, obj=obj, tag=f'coalesced -> {msg1} -> used {msg2}')
        else:
            # naively append, we are not doing "coalescing"
            self.trace(alloc=False, obj=obj, tag=f'new free -> cnt:{segm.blk_cnt: 6} start: {startix:06x} end: {startix+segm.blk_cnt:06x}')
            self.free_list.append((startix, segm.blk_cnt))

        obj.segm = None
        self.chk_subspace(obj, segm, self.space, is_already_allocd=False)
        assert len(self.space) == S

    def shrink(self):
        global_endix = self.global_endix
        to_be_released_fr_cnt = 0
        to_be_released_blk_cnt = 0
        for fr in reversed(self.free_list):
            if fr[0] + fr[1] == global_endix:
                to_be_released_fr_cnt += 1
                to_be_released_blk_cnt += fr[1]
                global_endix = fr[0]
            else:
                assert fr[0] + fr[1] < global_endix
                break

        assert(b == 0 for b in self.space[global_endix:])
        assert(len(self.space[global_endix:]) == to_be_released_blk_cnt)

        if to_be_released_fr_cnt:
            del self.free_list[-to_be_released_fr_cnt:]
            del self.space[global_endix:]


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

    free_blk_at_end_cnt = sum(1 for _ in itertools.takewhile(lambda b: b == 0, reversed(space)))

    print("Total blk cnt:", total_blk_cnt)
    print("Total file size:", (total_blk_cnt * BLK_SZ) / 1024, "kb")
    print()

    print("Free blk cnt:", free_blk_cnt)
    print("Free blk (at the end) cnt:", free_blk_at_end_cnt)
    print("Free size:", (free_blk_cnt * BLK_SZ) / 1024, "kb")
    print()
    print("External frag:", round((free_blk_cnt / total_blk_cnt) * 100, 2), "%")
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
    'pi-06', 'uk-01', 'uk-02', 'L', 'S']

_destacated_samples = {
        'L': 'uk-02', # large
        'S': 'dc-01', # small
        }

@click.command()
@click.option('--seed', default=31416)
@click.option('--rerun-until-bug', is_flag=True, default=False)
@click.option('-w', '--note-taker-back-w', default=12, help='cnt elements to shuffle')
@click.option('--dp', default=0.1*8, help='probability to delete a draw')
@click.option('--idp', default=0.01*8, help='probability to delete an "image" draw')
@click.option('--rf', default=0.25, help='scale blksz to add on reinserts, a random from [-blksz * rf, blksz * rf]')
@click.option('-a', '--alloc', type=click.Choice(['mono', 'kr']), required=True)
@click.option('-s', '--sample', type=click.Choice(_samples), default='ph-01')
@click.option('--coalescing', is_flag=True, default=False)
@click.option('-m', '--writer-model', type=click.Choice(['copier', 'notetaker', 'editor']), default='editor')
@click.option('--no-reinsert', is_flag=True, default=False)
@click.option('--shrink/--no-shrink', default=True)
@click.option('--trace/--no-trace', default=False)
def main(seed, rerun_until_bug, note_taker_back_w, dp, idp, rf, alloc, sample, coalescing, writer_model, no_reinsert, shrink, trace):
    SEED = seed
    NOTE_TAKER_BACK_W = max(4, note_taker_back_w)
    DEL_PROB = min(0.9, max(0, dp))
    DEL_IMG_PROB = min(0.9, max(0, idp))
    REINSERT_CHG_SZ_FACTOR = max(0.001, rf)
    ALLOCATOR = alloc
    SAMPLE_TARGET = _destacated_samples.get(sample, sample)
    COALESCING = coalescing
    WRITER_MODEL = writer_model
    REINSERT = not no_reinsert
    SHRINK = shrink
    TRACE = trace

    df = pd.read_csv('01-results/xopp-dataset-2023.csv')

    df2 = df[df['sample'] == SAMPLE_TARGET]
    sample_df = df2[['sample', 'type', 'char_length', 'widths_cnt', 'coords_cnt', 'text', 'page']]

    # Collect all the "draw" actions and objects
    _main_actions = []
    _main_obj_by_id = {}
    obj_id = 0
    for _, row in tqdm.tqdm(sample_df.iterrows()):
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

        sz = int(round(sz))
        if sz <= 0:
            continue

        obj = Obj(sz, desc_sz, obj_id, row['type'], int(row['page']), None)
        act = Action(False, obj.obj_id)
        _main_obj_by_id[obj_id] = obj
        _main_actions.append(act)

    do_simulations = True
    while do_simulations:
        do_simulations = False
        simulate(
                _main_actions = copy.deepcopy(_main_actions),
                _main_obj_by_id = copy.deepcopy(_main_obj_by_id),
                obj_id = obj_id,
                SEED = SEED,
                NOTE_TAKER_BACK_W = NOTE_TAKER_BACK_W,
                DEL_PROB = DEL_PROB,
                DEL_IMG_PROB = DEL_IMG_PROB,
                REINSERT_CHG_SZ_FACTOR = REINSERT_CHG_SZ_FACTOR,
                ALLOCATOR = ALLOCATOR,
                COALESCING = COALESCING,
                WRITER_MODEL = WRITER_MODEL,
                REINSERT = REINSERT,
                SHRINK = SHRINK,
                TRACE = TRACE
            )

        if rerun_until_bug:
            do_simulations = True
            rnd = random.Random(SEED)
            SEED = random.randint(0, 2**32)

def simulate(
        _main_actions,
        _main_obj_by_id,
        obj_id,
        SEED,
        NOTE_TAKER_BACK_W,
        DEL_PROB,
        DEL_IMG_PROB,
        REINSERT_CHG_SZ_FACTOR,
        ALLOCATOR,
        COALESCING,
        WRITER_MODEL,
        REINSERT,
        SHRINK,
        TRACE
    ):

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

            if REINSERT:
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
        allocator.trace_enabled = TRACE

        for act in tqdm.tqdm(actions, total=len(actions)):
            if act.is_delete_action:
                allocator.dealloc(act)
            else:
                allocator.alloc(act)

        if SHRINK:
            allocator.shrink()

        show_space_pages(space, obj_by_id, allocator)
        show_space_stats(space, obj_by_id, allocator)

if __name__ == '__main__':
    import sys
    main()
