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
    insert_generation : int

@dataclass
class Obj:
    data_sz : int
    desc_base_sz : int
    obj_id : int
    obj_type : str
    page_no : int
    segm : None

    def as_obj_id_str(self):
        return f"obj: {self.obj_id: 5} (pg: {self.page_no: 3})"
@dataclass
class Extent:
    blk_nr : int
    blk_cnt: int

@dataclass
class Segment:
    extents : list

    def as_indexes_str(self):
        ext = self.extents[0]
        return (f"cnt: {ext.blk_cnt: 5} "
                f"start: {ext.blk_nr:05x} "
                f"end: {ext.blk_nr + ext.blk_cnt:05x}"
                )

@dataclass
class AllocRequest:
    data_sz : int

@dataclass
class DeallocRequest:
    segm : Segment

@dataclass
class Response:
    segm : Segment
    expand_blk_space : int
    contract_blk_space : int
    expected_global_endix : int
    abort : bool

class Allocator:
    def alloc(self, req):
        # shall return a Response
        raise NotImplementedError()

    def dealloc(self, obj):
        # shall return a Response
        raise NotImplementedError()

class SimulatedAllocatorMixin:
    def single_extent_segment_for(self, data_sz, blk_nr=None):
        assert data_sz > 0
        subblock_data_sz = data_sz % BLK_SZ
        blk_cnt = (data_sz // BLK_SZ)

        if subblock_data_sz:
            blk_cnt += 1 # partially filled blk

        assert blk_cnt >= 0
        extent = Extent(blk_nr, blk_cnt)
        return Segment(extents=[extent])

class MonotonicAllocator(Allocator, SimulatedAllocatorMixin):
    def __init__(self):
        Allocator.__init__(self)
        self.global_endix = 0

    def alloc(self, req):
        # Always alloc on the top of the space
        blk_nr = self.global_endix
        segm = self.single_extent_segment_for(data_sz=req.data_sz, blk_nr=blk_nr)

        # A monotonic allocator always grow and expand
        # the space
        expand_blk_space = segm.extents[0].blk_cnt
        self.global_endix += segm.extents[0].blk_cnt

        return Response(
                segm=segm,
                expand_blk_space=expand_blk_space,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                abort=False
                )

    def dealloc(self, req):
        segm = req.segm

        # A monotonic allocator does not dealloc nothing
        # which it is pretty fast
        return Response(
                segm=segm,
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                abort=False
                )

    def contract(self):
        return Response(
                segm=None,
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                abort=False
                )

class KRAllocator(Allocator, SimulatedAllocatorMixin):
    def __init__(self, coalescing):
        Allocator.__init__(self)
        self.global_endix = 0
        self.coalescing = coalescing

        self.free_list = []

    def alloc(self, req):
        segm = self.single_extent_segment_for(data_sz=req.data_sz)
        ext = segm.extents[0]

        # Optimizations: order the list by size so we can
        # discard too-small entries quickly
        for i in range(len(self.free_list)):
            fr_ix, fr_cnt = self.free_list[i]

            # Sanity check: the free blocks are within
            # the space boundaries
            assert fr_ix >= 0 and fr_cnt > 0
            assert self.global_endix >= fr_ix+fr_cnt

            if fr_cnt == ext.blk_cnt:
                # best fit, unlink from the free list
                del self.free_list[i]
                ext.blk_nr = fr_ix

                return Response(
                        segm=segm,
                        expand_blk_space=0,
                        contract_blk_space=0,
                        expected_global_endix=self.global_endix,
                        abort=False
                        ) # TODO tag 'perfect'

            elif fr_cnt > ext.blk_cnt:
                # good enough fit, update in place the free list
                self.free_list[i] = (fr_ix + ext.blk_cnt, fr_cnt - ext.blk_cnt)
                ext.blk_nr = fr_ix

                #self.trace(alloc=True, obj=obj, tag=f'split -> new free cnt:{self.free_list[i][1]: 6} start: {self.free_list[i][0]:06x} end: {self.free_list[i][0]+self.free_list[i][1]:06x}')

                return Response(
                        segm=segm,
                        expand_blk_space=0,
                        contract_blk_space=0,
                        expected_global_endix=self.global_endix,
                        abort=False
                        ) # TODO tag


        # no fit at all, alloc more space
        ext.blk_nr = self.global_endix
        self.global_endix += ext.blk_cnt

        return Response(
                segm=segm,
                expand_blk_space=ext.blk_cnt,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                abort=False
                )


    def dealloc(self, req):
        segm = req.segm

        for ext in segm.extents:
            startix = ext.blk_nr
            endix = startix+ext.blk_cnt

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
                        #  fr_cnt   ext.blk_cnt
                        found.append((fr_ix, fr_blk_cnt))
                        coalesced = self.free_list[i] = (fr_ix, fr_blk_cnt+ext.blk_cnt)

                    elif endix == fr_ix:
                        #         startix    fr_ix
                        #         v          v
                        #         ^..........^.....
                        #       ext.blk_cnt |   fr_cnt
                        #                  endix
                        found.append((fr_ix, fr_blk_cnt))
                        if coalesced:
                            prev_fr_ix, prev_fr_blk_cnt = self.free_list[i-1]
                            coalesced = self.free_list[i-1] = (prev_fr_ix, prev_fr_blk_cnt+fr_blk_cnt)
                            del self.free_list[i]
                        else:
                            coalesced = self.free_list[i] = (startix, ext.blk_cnt+fr_blk_cnt)

                        break

                    elif fr_ix > endix:
                        break

                if not coalesced:
                    #self.trace(alloc=False, obj=obj, tag=f'new free -> cnt:{ext.blk_cnt: 6} start: {startix:06x} end: {startix+ext.blk_cnt:06x}')
                    self.free_list.append((startix, ext.blk_cnt))
                else:
                    #msg1 = f'cnt:{coalesced[1]: 6} start: {coalesced[0]:06x} end: {coalesced[0]+coalesced[1]:06x}'
                    #msg2 = '; '.join(f'cnt:{f[1]: 6} start: {f[0]:06x} end: {f[0]+f[1]:06x}' for f in found)
                    #self.trace(alloc=False, obj=obj, tag=f'coalesced -> {msg1} -> used {msg2}')
                    pass
            else:
                # naively append, we are not doing "coalescing"
                #self.trace(alloc=False, obj=obj, tag=f'new free -> cnt:{ext.blk_cnt: 6} start: {startix:06x} end: {startix+ext.blk_cnt:06x}')
                self.free_list.append((startix, ext.blk_cnt))

        return Response(
                segm=segm,
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                abort=False
                )

    def contract(self):
        global_endix = self.global_endix
        to_be_released_fr_cnt = 0
        to_be_released_blk_cnt = 0
        for fr in sorted(self.free_list, reverse=True):
            if fr[0] + fr[1] == global_endix:
                to_be_released_fr_cnt += 1
                to_be_released_blk_cnt += fr[1]
                global_endix = fr[0]
            else:
                assert fr[0] + fr[1] < global_endix
                break


        if to_be_released_fr_cnt:
            del self.free_list[-to_be_released_fr_cnt:]
            self.global_endix = global_endix

        return Response(
                segm=None,
                expand_blk_space=0,
                contract_blk_space=to_be_released_blk_cnt,
                expected_global_endix=self.global_endix,
                abort=False
                )


class Simulator:
    def __init__(self, allocator, space, obj_by_id, trace_enabled):
        self.allocator = allocator
        self.space = space
        self.obj_by_id = obj_by_id
        self.trace_enabled = trace_enabled

    def alloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=False)

        resp = self.allocator.alloc(AllocRequest(obj.data_sz))
        self.trace(type='alloc', obj=obj, segm=resp.segm)

        assert not resp.abort
        assert resp.expand_blk_space >= 0
        assert resp.contract_blk_space == 0

        if resp.expand_blk_space:
            self.trace(type='expand', amount=resp.expand_blk_space)
            self.space.extend([0] * resp.expand_blk_space)

        assert resp.expected_global_endix == len(self.space)

        # Store the object in the space and check
        # that the obj's segment makes sense and that the
        # space allocated is freed *before* the store and
        # not-freed *after* the store
        self.store(obj, resp.segm)

        # Sanity check: the space may grow only by the amount
        # expand_blk_space (if any)
        assert len(self.space) == S + resp.expand_blk_space

    def dealloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=True)

        resp = self.allocator.dealloc(DeallocRequest(obj.segm))
        self.trace(type='dealloc', obj=obj, segm=resp.segm)

        assert not resp.abort
        assert resp.expand_blk_space == 0
        assert resp.contract_blk_space >= 0

        self.remove(obj)

        if resp.contract_blk_space:
            self.trace(type='contract', amount=resp.contract_blk_space)
            assert len(self.space) >= resp.contract_blk_space
            assert all(b == 0 for b in self.space[-resp.contract_blk_space:])
            del self.space[-resp.contract_blk_space:]

        assert resp.expected_global_endix == len(self.space)
        assert len(self.space) == S - resp.contract_blk_space

    def contract(self):
        S = len(self.space)

        resp = self.allocator.contract()

        assert not resp.abort
        assert resp.expand_blk_space == 0
        assert resp.contract_blk_space >= 0

        if resp.contract_blk_space:
            self.trace(type='contract', amount=resp.contract_blk_space)
            assert len(self.space) >= resp.contract_blk_space
            assert all(b == 0 for b in self.space[-resp.contract_blk_space:])
            del self.space[-resp.contract_blk_space:]

        assert resp.expected_global_endix == len(self.space)
        assert len(self.space) == S - resp.contract_blk_space

    def store(self, obj, segm):
        ''' Store the object in the space using the blocks allocated
            for it in segm segment.

            It is expected that the space be freed (zeroed) *before*
            the store and filled (non-zeroed) *after* the store.

            This method check both expectations.

            After calling this method, obj.segm is set to the passed segm.
        '''
        self.chk_subspace(obj, segm, self.space, is_already_allocd=False)

        # Assign the allocated segment to the object
        # after checking the subspace is_already_allocd=False
        # but before calling chk_subspace with is_already_allocd=True
        obj.segm = segm

        for ext in obj.segm.extents:
            startix = ext.blk_nr
            endix = startix + ext.blk_cnt

            self.space[startix:endix] = [obj.obj_id] * ext.blk_cnt

        self.chk_subspace(obj, obj.segm, self.space, is_already_allocd=True)

    def remove(self, obj):
        ''' Remove the object in the space in the assigned blocks allocated
            for it in obj.segm segment.

            It is expected that the space be freed (zeroed) *after*
            the remove and filled (non-zeroed) *before* the remove.

            This method check both expectations.

            After calling this method, obj.segm is set to None
        '''
        self.chk_subspace(obj, obj.segm, self.space, is_already_allocd=True)

        for ext in obj.segm.extents:
            startix = ext.blk_nr
            endix = startix + ext.blk_cnt

            self.space[startix:endix] = [0] * ext.blk_cnt

        # unlink the segment from the object so it is officially freed
        # before checking the subspace with is_already_allocd=False
        # but after calling chk_subspace with is_already_allocd=True
        segm = obj.segm
        obj.segm = None

        self.chk_subspace(obj, segm, self.space, is_already_allocd=False)


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
            assert value != 0

            # Expected to have a segment because it is already
            # allocated
            assert obj.segm is not None
            assert obj.segm.extents

            # Sanity checks on extents
            assert segm is not None
            assert segm.extents
            assert len(obj.segm.extents) == len(segm.extents)

            # Check both obj.segm and segm are the "same"
            for oext, ext in zip(obj.segm.extents, segm.extents):
                assert ext.blk_nr is None or oext.blk_nr == ext.blk_nr
                assert oext.blk_cnt == ext.blk_cnt

                assert oext.blk_nr >= 0
                assert oext.blk_cnt > 0

        else:
            value = 0
            assert obj.segm is None

        # The extents cannot overlap each other
        # TODO

        for ext in segm.extents:
            startix = ext.blk_nr
            endix = startix + ext.blk_cnt

            # Ensure that the blks are within the space boundaries
            assert len(space[startix:endix]) == ext.blk_cnt

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

    def object_lookup(self, act, is_delete_action):
        ''' Lookup the object referenced by the action and perform
            some sanity checks
        '''
        obj = self.obj_by_id[act.obj_id]
        assert obj.obj_id == act.obj_id
        assert obj.obj_id != 0

        if is_delete_action:
            assert act.is_delete_action
            assert obj.segm is not None
            assert obj.segm.extents
            for ext in obj.segm.extents:
                assert ext.blk_nr >= 0
                assert ext.blk_cnt > 0
        else:
            assert not act.is_delete_action
            assert obj.segm is None

        return obj

    def trace(self, type, obj=None, segm=None, amount=None):
        if not self.trace_enabled:
            return

        if type == 'alloc':
            print(
                "A ",
                obj.as_obj_id_str(),
                segm.as_indexes_str(),
                )
        elif type == 'dealloc':
            print(
                " D",
                obj.as_obj_id_str(),
                segm.as_indexes_str(),
                )
        elif type == 'expand':
            print(
                "E ",
                amount,
                "expand",
                len(self.space),
                "->",
                len(self.space)+amount
                )
        elif type == 'contract':
            print(
                " R",
                amount,
                "contract",
                len(self.space),
                "->",
                len(self.space)-amount
                )
        else:
            assert False




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
    total_data_sz = sum(obj.data_sz for obj in obj_by_id.values() if obj.segm is not None)

    total_blk_cnt = len(space)
    non_free_blk_cnt = sum(1 if b != 0 else 0 for b in space)

    free_blk_cnt = total_blk_cnt - non_free_blk_cnt
    free_blk_at_end_cnt = sum(1 for _ in itertools.takewhile(lambda b: b == 0, reversed(space)))

    total_file_sz = total_blk_cnt * BLK_SZ

    internal_frag_sz = (non_free_blk_cnt * BLK_SZ) - total_data_sz
    assert internal_frag_sz >= 0


    print("Block cnt:", total_blk_cnt)
    print("File size:", total_file_sz / 1024, "kb")
    print("Useful data size:", total_data_sz / 1024, "kb")
    print()

    print("Free block cnt:", free_blk_cnt)
    print("Free block (at the end) cnt:", free_blk_at_end_cnt)
    print("Free size:", (free_blk_cnt * BLK_SZ) / 1024, "kb")
    print()
    print("External frag:", round((free_blk_cnt / total_blk_cnt) * 100, 2), "% of blocks are freed/unused")
    print("Internal frag:", round((internal_frag_sz / total_data_sz) * 100, 2), "% of data is reserved but wasted (doesn't contain useful data)")
    print()

    print("Minimum theoretical total blk cnt:", non_free_blk_cnt)
    print("Minimum theoretical file size:", (non_free_blk_cnt * BLK_SZ) / 1024, "kb")
    print()

SAMPLE_TARGET = 'ph-01'
#SAMPLE_TARGET = 'dc-03'
#SAMPLE_TARGET = 'uk-02'

_destacated_samples = {
        'lot':   'uk-02', # a lot of draws, random size
        'few':   'dc-01', # very few draws some really small in size, other really large
        'small': 'fo-03', # ~2k draws most really small in size (p77 < 1.5k), very few are large (14k)
        }

_samples = ['au-01', 'dc-01', 'dc-02', 'dc-03', 'dc-04', 'dc-05', 'dc-06',
    'dc-07', 'dw-01', 'et-01', 'et-02', 'et-03', 'et-04', 'fo-01',
    'fo-02', 'fo-03', 'fo-04', 'fo-05', 'fo-06', 'fo-07', 'in-01',
    'in-03', 'in-04', 'in-05', 'in-06', 'in-07', 'in-08', 'in-09',
    'in-10', 'in-11', 'in-12', 'in-13', 'in-14', 'ph-01', 'ph-03',
    'ph-05', 'ph-06', 'pi-01', 'pi-02', 'pi-03', 'pi-04', 'pi-05',
    'pi-06', 'uk-01', 'uk-02'] + list(_destacated_samples.keys())

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
@click.option('--contract/--no-contract', default=True)
@click.option('--trace/--no-trace', default=False)
@click.option('--show-pages/--no-show-pages', default=False)
def main(seed, rerun_until_bug, note_taker_back_w, dp, idp, rf, alloc, sample, coalescing, writer_model, no_reinsert, contract, trace, show_pages):
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
    CONTRACT = contract
    TRACE = trace
    SHOW_PAGES = show_pages

    df = pd.read_csv('01-results/xopp-dataset-2023.csv')

    df2 = df[df['sample'] == SAMPLE_TARGET]
    sample_df = df2[['sample', 'type', 'char_length', 'widths_cnt', 'coords_cnt', 'text', 'page']]

    # Collect all the "draw" actions and objects
    _main_actions = []
    _main_obj_by_id = {}
    obj_id = 0
    for _, row in tqdm.tqdm(sample_df.iterrows(), total=len(sample_df)):
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
        act = Action(False, obj.obj_id, 0)
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
                CONTRACT = CONTRACT,
                TRACE = TRACE,
                SHOW_PAGES = SHOW_PAGES
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
        CONTRACT,
        TRACE,
        SHOW_PAGES
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
    T = tqdm.tqdm()
    while i < L:
        act = editor_actions[i]
        T.update()

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
                act.insert_generation += 1
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
    T.close()

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
            allocator = MonotonicAllocator()
        elif ALLOCATOR == 'kr':
            allocator = KRAllocator(coalescing=COALESCING)
        else:
            assert False

        sim = Simulator(allocator, space, obj_by_id, trace_enabled=TRACE)

        TA = tqdm.tqdm(total=sum(1 for act in actions if not act.is_delete_action), position=1, disable=TRACE)
        TD = tqdm.tqdm(total=sum(1 for act in actions if act.is_delete_action), position=2, disable=TRACE)
        for act in tqdm.tqdm(actions, total=len(actions), position=0, disable=TRACE):
            if act.is_delete_action:
                sim.dealloc(act)
                TD.update()
            else:
                sim.alloc(act)
                TA.update()

        TD.close()
        TA.close()

        if CONTRACT:
            sim.contract()

        print()
        if SHOW_PAGES:
            show_space_pages(space, obj_by_id, allocator)
        show_space_stats(space, obj_by_id, allocator)

if __name__ == '__main__':
    import sys
    main()
