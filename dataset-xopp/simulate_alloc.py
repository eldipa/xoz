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

import PIL
from PIL import ImageDraw
import tqdm
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt

BLK_SZ = 512

import collections, copy, random, itertools
import dataclasses
from dataclasses import dataclass

def kb(bytes_sz):
    if bytes_sz > 1024**2:
        return round(bytes_sz / 1024)

    n = round(bytes_sz / 1024, 2)
    if n < 0 and bytes_sz > 0:
        n = round(bytes_sz / 1024, 4)

    return n

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
        return f"obj:{self.obj_id: 4} (pg:{self.page_no: 3})"

@dataclass(order=True)
class Extent:
    blk_nr : int
    blk_cnt: int

    def as_indexes_str(self):
        return f"{self.blk_nr:05x} {self.blk_nr+self.blk_cnt:05x} [{self.blk_cnt: 4}]"

    def as_tuple(self):
        return (self.blk_nr, self.blk_cnt)

    def as_endpoints(self):
        return (self.blk_nr, self.blk_nr + self.blk_cnt)

@dataclass
class Segment:
    extents : list

    def as_indexes_str(self):
        return f'exts: {len(self.extents): 2}' + ' {' + ", ".join(ext.as_indexes_str() for ext in self.extents) + '}'

@dataclass
class AllocRequest:
    data_sz : int
    allow_expand : bool

@dataclass
class DeallocRequest:
    segm : Segment

@dataclass
class Response:
    segm : Segment
    expand_blk_space : int
    contract_blk_space : int
    expected_global_endix : int
    not_enough_space : bool
    traces : list

    hint_closest_free_blk_cnt : int

    def add(self, resp, ignore_hints=True):
        self.segm.extents.extend(resp.segm.extents)

        self.expand_blk_space = max(self.expand_blk_space, resp.expand_blk_space)
        self.contract_blk_space = max(self.contract_blk_space, resp.contract_blk_space)
        self.expected_global_endix = max(self.expected_global_endix, resp.expected_global_endix)

        self.not_enough_space = self.not_enough_space or resp.not_enough_space
        self.traces.extend(resp.traces)

        if not ignore_hints:
            for field in dataclasses.fields(resp):
                if not field.name.startswith("hint_"):
                    continue

                new_value = getattr(resp, field.name)
                if new_value is None:
                    continue

                setattr(self, field.name, new_value)

    def trace(self, *args):
        if not args:
            return
        self.traces.append(tuple(args))

    def update(self, **kargs):
        params = dict(
                segm=Segment([]),
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=0,
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None # no hint
                )

        params.update(**kargs)
        self.add(Response(**params), ignore_hints=False)


class Allocator:
    def alloc(self, req):
        # shall return a Response
        raise NotImplementedError()

    def dealloc(self, obj):
        # shall return a Response
        raise NotImplementedError()

    def contract(self):
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
        if not req.allow_expand:
            # This makes little sense for a MonotonicAllocator !!
            return Response(
                    segm=None,
                    expand_blk_space=0,
                    contract_blk_space=0,
                    expected_global_endix=self.global_endix,
                    not_enough_space=True,
                    traces=[],
                    hint_closest_free_blk_cnt=None
                    )

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
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
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
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
                )

    def contract(self):
        return Response(
                segm=None,
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
                )

class KRAllocator(Allocator, SimulatedAllocatorMixin):
    def __init__(self, coalescing, min_fr_split_remain):
        Allocator.__init__(self)
        self.global_endix = 0
        self.coalescing = coalescing
        self.min_fr_split_remain = min_fr_split_remain

        self.free_list = []

    def alloc(self, req):
        segm = self.single_extent_segment_for(data_sz=req.data_sz)
        ext = segm.extents[0]

        main_resp = Response(
                        segm=segm,
                        expand_blk_space=0,
                        contract_blk_space=0,
                        expected_global_endix=self.global_endix,
                        not_enough_space=False,
                        traces=[],
                        hint_closest_free_blk_cnt=None
                        )

        closest_free_blk_cnt = -1

        # Optimizations: order the list by size so we can
        # discard too-small entries quickly
        for i in range(len(self.free_list)):
            fr_ix, fr_cnt = self.free_list[i].as_tuple()

            # Sanity check: the free blocks are within
            # the space boundaries
            assert fr_ix >= 0 and fr_cnt > 0
            assert self.global_endix >= fr_ix+fr_cnt

            if fr_cnt == ext.blk_cnt:
                # best fit, unlink from the free list
                fr = self.free_list[i]
                del self.free_list[i]
                ext.blk_nr = fr_ix

                main_resp.trace('perfect free used:', fr)
                return main_resp

            elif fr_cnt > ext.blk_cnt:
                if fr_cnt - ext.blk_cnt < self.min_fr_split_remain:
                    continue

                # good enough fit, update in place the free list
                self.free_list[i] = Extent(fr_ix + ext.blk_cnt, fr_cnt - ext.blk_cnt)
                ext.blk_nr = fr_ix

                main_resp.trace('split free, remain:', self.free_list[i])
                return main_resp
            else:
                closest_free_blk_cnt = max(closest_free_blk_cnt, fr_cnt)

        if req.allow_expand:
            # no fit at all, alloc more space
            ext.blk_nr = self.global_endix
            self.global_endix += ext.blk_cnt

            main_resp.update(
                    expand_blk_space=ext.blk_cnt,
                    expected_global_endix=self.global_endix
                    )
            return main_resp

        else:
            # we are not allow to expand the space (request more)
            # so we fail
            main_resp.update(
                    not_enough_space=True,
                    hint_closest_free_blk_cnt=max(closest_free_blk_cnt, 0)
                    )
            return main_resp


    def dealloc(self, req):
        segm = req.segm

        main_resp = Response(
                segm=segm,
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=self.global_endix,
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
                )

        for ext in segm.extents:
            startix = ext.blk_nr
            endix = startix+ext.blk_cnt

            if self.coalescing:
                found = []
                coalesced = None

                self.free_list = list(sorted(self.free_list))
                for i in range(len(self.free_list)):
                    fr_ix, fr_blk_cnt = self.free_list[i].as_tuple()

                    if fr_ix + fr_blk_cnt == startix:
                        # fr_ix   startix
                        # v       v
                        # ^.......^....
                        #  fr_cnt   ext.blk_cnt
                        found.append((fr_ix, fr_blk_cnt))
                        coalesced = self.free_list[i] = Extent(fr_ix, fr_blk_cnt+ext.blk_cnt)

                    elif endix == fr_ix:
                        #         startix    fr_ix
                        #         v          v
                        #         ^..........^.....
                        #       ext.blk_cnt |   fr_cnt
                        #                  endix
                        found.append((fr_ix, fr_blk_cnt))
                        if coalesced:
                            prev_fr_ix, prev_fr_blk_cnt = self.free_list[i-1].as_tuple()
                            coalesced = self.free_list[i-1] = Extent(prev_fr_ix, prev_fr_blk_cnt+fr_blk_cnt)
                            del self.free_list[i]
                        else:
                            coalesced = self.free_list[i] = Extent(startix, ext.blk_cnt+fr_blk_cnt)

                        break

                    elif fr_ix > endix:
                        break

                if not coalesced:
                    fr = Extent(startix, ext.blk_cnt)
                    main_resp.trace("free added:", fr)
                    self.free_list.append(fr)
                else:
                    main_resp.trace("free coalesced:", *found)
                    main_resp.trace("coalesced into:", coalesced)
            else:
                # naively append, we are not doing "coalescing"
                fr = Extent(startix, ext.blk_cnt)
                main_resp.trace("free added:", fr)
                self.free_list.append(fr)

        return main_resp

    def contract(self):
        global_endix = self.global_endix
        to_be_released_fr_cnt = 0
        to_be_released_blk_cnt = 0
        for fr in sorted(self.free_list, reverse=True):
            if fr.blk_nr + fr.blk_cnt == global_endix:
                to_be_released_fr_cnt += 1
                to_be_released_blk_cnt += fr.blk_cnt
                global_endix = fr.blk_nr
            else:
                assert fr.blk_nr + fr.blk_cnt < global_endix
                break


        if to_be_released_fr_cnt:
            del self.free_list[-to_be_released_fr_cnt:]
            self.global_endix = global_endix

        return Response(
                segm=None,
                expand_blk_space=0,
                contract_blk_space=to_be_released_blk_cnt,
                expected_global_endix=self.global_endix,
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
                )

class HalvingAllocator(Allocator):
    def __init__(self, backend_allocator, max_extent_cnt):
        Allocator.__init__(self)
        self.backend_allocator = backend_allocator
        self.max_extent_cnt = max_extent_cnt

    def alloc(self, req):
        main_resp = Response(
                segm=Segment([]),
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=0,
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
                )

        # never split if it is too small
        if req.data_sz <= BLK_SZ:
            main_resp.trace("too small, no split")
            main_resp.add(self.backend_allocator.alloc(req))
            return main_resp

        # the data is much larger than what a single extent
        # can manage: we are forced to do a split
        chunks = self.chunkinize(req.data_sz, (BLK_SZ << 16) - 1)
        if len(chunks) > 1:
            main_resp.trace(f"too large, forcibly split into {len(chunks)} chks")

        halving_order = 0
        for chk in chunks:
            main_resp, halving_order = self.try_alloc_without_expand(
                    main_resp=main_resp,
                    data_sz=chk,
                    too_small_threshold=BLK_SZ,
                    req_allow_expand=req.allow_expand,
                    halving_order=halving_order
                    )

            if main_resp.not_enough_space:
                return main_resp

        return main_resp

    def try_alloc_without_expand(self, main_resp, data_sz, too_small_threshold, req_allow_expand, halving_order):
        ''' Allocate the given size (data_sz) splitting it into smaller chunks
            and allocating them individually trying always to not expand/grow
            the space.

            The method will try to allocate data_sz in one try first, splitting
            it into chunks of half size if the allocation failed and trying to
            allocate them instead.

            On each failure the chunk is halved again until too_small_threshold
            is reached. In that case the remaining unallocated chunks are
            allocated in one shot expanding the space.

            If req_allow_expand is False it means that the space cannot
            be expanded in any case so a not_enough_space is returned.

            The last halving order used is returned. Next calls
            to try_alloc_without_expand within the same alloc() call
            should use the same order as it makes sense because
            smaller orders would imply bigger chks and those would
            not be allocated (we already tried)
        '''
        MAX_HALVING_ORDER = 31
        remain = data_sz

        if (remain >> halving_order) < too_small_threshold:
            chunks = [remain]
            last_try = True
        else:
            chunks = self.chunkinize(remain, remain >> halving_order)
            last_try = False

        while remain > 0:
            main_resp.trace(f"halved {halving_order} times: {len(chunks)} chks remains, sizes {kb(chunks[0])} kb & {kb(chunks[-1])} kb")

            for chk in chunks:
                resp = self.backend_allocator.alloc(AllocRequest(
                    data_sz = chk,
                    allow_expand = last_try and req_allow_expand
                    ))

                if resp.not_enough_space and not last_try:
                    # If the next chunks' size is too small, do not chunkinize
                    # and instead try to allocate all in one shot
                    # but allowing expand the space in a "last try"
                    #
                    # Another case to stop splitting and do a last try is when
                    # halving_order is the maximum which means that if allow another
                    # halving round the order will be too large.
                    if (remain >> halving_order) < too_small_threshold or halving_order == MAX_HALVING_ORDER:
                        chunks = [remain]
                        last_try = True
                        break

                    # Because the current chk couldn't be allocated
                    # without expanding the space, we take all the
                    # remaining sz to be allocated and (re)chunkinize
                    # it in chunks of half the previous chk size.
                    halving_order += 1
                    chunks = self.chunkinize(remain, remain >> halving_order)
                    break
                else:
                    main_resp.add(resp)

                    # If not not_enough_space it is because we really couldn't
                    # allocate and will never do. Return not_enough_space.
                    if resp.not_enough_space:
                        main_resp.not_enough_space = True
                        return main_resp, halving_order

                    # Nice, one chk allocated, one chk less to be allocated
                    remain -= chk

        return main_resp, halving_order

    def chunkinize(self, orig_sz, chk_sz):
        chunks = [chk_sz for n in range(orig_sz // chk_sz)]
        remain = orig_sz % chk_sz
        if remain:
            chunks.append(remain)

        return chunks


    def dealloc(self, req):
        return self.backend_allocator.dealloc(req)

    def contract(self):
        return self.backend_allocator.contract()

class LinearAllocator(Allocator):
    def __init__(self, backend_allocator, max_extent_cnt):
        Allocator.__init__(self)
        self.backend_allocator = backend_allocator
        self.max_extent_cnt = max_extent_cnt

    def alloc(self, req):
        main_resp = Response(
                segm=Segment([]),
                expand_blk_space=0,
                contract_blk_space=0,
                expected_global_endix=0,
                not_enough_space=False,
                traces=[],
                hint_closest_free_blk_cnt=None
                )

        # never split if it is too small
        if req.data_sz <= BLK_SZ:
            main_resp.trace("too small, no split")
            main_resp.add(self.backend_allocator.alloc(req))
            return main_resp

        # the data is much larger than what a single extent
        # can manage: we are forced to do a split
        chunks = self.chunkinize(req.data_sz, (BLK_SZ << 16) - 1)
        if len(chunks) > 1:
            main_resp.trace(f"too large, forcibly split into {len(chunks)} chks")

        # initially we start trying to allocate the whole chunk
        # (aka we expect to not split)
        probe_sz = chunks[0]
        for chk in chunks:
            main_resp, probe_sz = self.try_alloc_without_expand(
                    main_resp=main_resp,
                    data_sz=chk,
                    too_small_threshold=BLK_SZ,
                    req_allow_expand=req.allow_expand,
                    probe_sz=probe_sz
                    )

            if main_resp.not_enough_space:
                return main_resp

        return main_resp

    def try_alloc_without_expand(self, main_resp, data_sz, too_small_threshold, req_allow_expand, probe_sz):
        remain = data_sz
        last_try = False
        ext_cnt = 0
        while remain > 0:
            probe_sz = min(probe_sz, remain)
            if ext_cnt >= self.max_extent_cnt:
                main_resp.trace(f"probe give up: too frag")
                probe_sz = remain
                last_try = True

            resp = self.backend_allocator.alloc(AllocRequest(
                data_sz = probe_sz,
                allow_expand = last_try and req_allow_expand
                ))

            if resp.not_enough_space and not last_try:
                if resp.hint_closest_free_blk_cnt is not None:
                    probe_sz = resp.hint_closest_free_blk_cnt * BLK_SZ
                else:
                    probe_sz -= BLK_SZ//2 # TODO

                if probe_sz < too_small_threshold:
                    main_resp.trace(f"probe give up: probe {kb(probe_sz)} kb smaller than threshold {kb(too_small_threshold)} kb")

                    probe_sz = remain
                    last_try = True

                elif (remain // probe_sz) + ext_cnt >= self.max_extent_cnt:
                    main_resp.trace(f"probe give up: probe {kb(probe_sz)} kb would generate too frag ({(remain // probe_sz) + ext_cnt} exts at best)")

                    probe_sz = remain
                    last_try = True

                else:
                    hint_msg = "hint None" if resp.hint_closest_free_blk_cnt is None else f"hint [{resp.hint_closest_free_blk_cnt: 3}]"
                    main_resp.trace(f"probe {kb(probe_sz)} kb, remain {kb(remain)} kb, {hint_msg}")
            else:
                main_resp.add(resp)

                # If not not_enough_space it is because we really couldn't
                # allocate and will never do. Return not_enough_space.
                if resp.not_enough_space:
                    main_resp.not_enough_space = True
                    return main_resp, probe_sz

                # Nice, one chk allocated, one chk less to be allocated
                remain -= probe_sz
                ext_cnt += 1

        return main_resp, probe_sz

    def chunkinize(self, orig_sz, chk_sz):
        chunks = [chk_sz for n in range(orig_sz // chk_sz)]
        remain = orig_sz % chk_sz
        if remain:
            chunks.append(remain)

        return chunks


    def dealloc(self, req):
        return self.backend_allocator.dealloc(req)

    def contract(self):
        return self.backend_allocator.contract()

class Simulator:
    def __init__(self, allocator, space, obj_by_id, trace_enabled):
        self.allocator = allocator
        self.space = space
        self.obj_by_id = obj_by_id
        self.trace_enabled = trace_enabled

    def alloc(self, act):
        S = len(self.space)
        obj = self.object_lookup(act, is_delete_action=False)

        resp = self.allocator.alloc(AllocRequest(obj.data_sz, True))
        self.trace(type='alloc', obj=obj, segm=resp.segm, subtraces=resp.traces)

        assert not resp.not_enough_space
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
        self.trace(type='dealloc', obj=obj, segm=resp.segm, subtraces=resp.traces)

        assert not resp.not_enough_space
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
        self.trace(type='contract', amount=resp.contract_blk_space, subtraces=resp.traces)

        assert not resp.not_enough_space
        assert resp.expand_blk_space == 0
        assert resp.contract_blk_space >= 0

        if resp.contract_blk_space:
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

        # Return a flatten (start, end, start, end, start, end, ....) of the sorted
        # extents
        endpoints = sum((ext.as_endpoints() for ext in sorted(segm.extents)), ())
        all_startixs = endpoints[0::2]
        all_endixs = endpoints[1::2]
        assert len(all_startixs) == len(all_endixs) == len(segm.extents)

        # The extents cannot overlap each other
        # We check this verifying that the startix of the next (right) extent
        # is greater or equal to the endix of the previous (left) extent
        # Requires that the extents are ordered from lower to higher startix.
        #
        #           /- endix1 == startix2  (no overlap, just contiguous)
        #           V
        #   s1    e1s2       e2
        #   |.......|-------|
        #
        #           /----/-  endix1 < startix2  (no overlap, gap between)
        #           V   V
        #   s1     e1  s2       e2
        #   |.......|   |-------|
        #
        #           /----/-  endix1 > startix2  (overlap, bad)
        #           V   V
        #   s1     s2   e1      e2
        #   |.......|:::|-------|
        assert all(prev_endix <= next_startix for prev_endix, next_startix in zip(all_endixs[:-1], all_startixs[1:]))

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
            #
            # These check may be skipped if the extent that follows
            # the current one or that presides is from the same segment
            # object so both will have the same object id not because
            # and overflow but because the data is stored contiguously.
            if endix not in all_startixs:
                assert all(b != obj.obj_id for b in space[endix:endix+1])

            if startix not in all_endixs:
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

    def trace(self, type, obj=None, segm=None, amount=None, subtraces=None):
        if not self.trace_enabled:
            return

        if type == 'alloc':
            print(
                "A ",
                obj.as_obj_id_str(),
                f"{kb(obj.data_sz): 3} kb",
                segm.as_indexes_str(),
                )
        elif type == 'dealloc':
            print(
                " D",
                obj.as_obj_id_str(),
                f"{kb(obj.data_sz): 3} kb",
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

        self._trace_subtraces(
            prefix = " " * 48,
            subtraces = subtraces
            )

    def _trace_subtraces(self, prefix, subtraces):
        if not subtraces:
            return
        else:
            for subtrace in subtraces:
                ret = []
                for tag in subtrace:
                    if isinstance(tag, Segment):
                        ret.append(tag.as_indexes_str())
                    elif isinstance(tag, Extent):
                        ret.append(tag.as_indexes_str())
                    elif isinstance(tag, str):
                        ret.append(tag)
                    else:
                        ret.append(str(tag))
                print(prefix, *ret)


class BuddyAllocator:
    pass

class SegAllocator:
    pass

class PageAllocator:
    pass

def show_space_obj_ids(space, obj_by_id, allocator, fname_postfix):
    print("Object IDs map:")
    W = 30
    for i, b in enumerate(space, 1):
        if b == 0:
            char = f'.... '
        else:
            char = f'{b:04x} '
        print(char, end='')
        if (i % W) == 0:
            print() # newline
    print()

def show_space_obj_types(space, obj_by_id, allocator, fname_postfix):
    print("Object types map:")
    W = 60
    for i, b in enumerate(space, 1):
        if b:
            obj = obj_by_id[b]
            char = obj.obj_type.upper() * 2
        else:
            char = '..'
        print(char + ' ', end='')
        if (i % W) == 0:
            print() # newline
    print()

def img_space_obj_types(space, obj_by_id, allocator, fname_postfix):
    L = 8

    S = len(space)
    if (S**0.5) < 80:
        COLS = 40
    else:
        COLS = 80

    width = L * COLS
    height = L * (len(space) // COLS)
    if len(space) % COLS != 0:
        height += L

    img = PIL.Image.new(
            size=(width, height),
            mode="RGB"
            )

    def to_color_ints(color_floats):
        return tuple(round(c * 255) for c in color_floats)

    sns_colors = [to_color_ints(c) for c in sns.color_palette('deep')]
    color_by_obj_type = {
            's': sns_colors[0],  # blue
            't': sns_colors[1],  # orange
            'i': sns_colors[2],  # green
            'x': sns_colors[3],  # red
            }

    sns_colors = [to_color_ints(c) for c in sns.color_palette('pastel')]
    color_for_empty_blk = sns_colors[7]  # grey

    row, col = 0, 0
    for b in space:
        if b:
            obj = obj_by_id[b]
            color = color_by_obj_type[obj.obj_type]
        else:
            color = color_for_empty_blk

        for x in range(col*L, (col+1)*L):
            for y in range(row*L, (row+1)*L):
                img.putpixel((x, y), color)

        col += 1
        if col % COLS == 0:
            col = 0
            row += 1

    img.save(f"types_{fname_postfix}.png")

import itertools

def run_length_encode(seq):
    return ((grp, sum(1 for _ in vals_iter)) for grp, vals_iter in itertools.groupby(seq))

def img_space_frag(space, obj_by_id, allocator, fname_postfix):
    sns_colors = sns.color_palette("flare_r")

    color_map_fn = create_alternating_color_map({
        True:  [sns_colors[0], sns_colors[1]],  # for not alloc
        False: [sns_colors[-3], sns_colors[-2]], # for alloc
        })

    plot_run_lengths(
            runs=[(b == 0, runlength) for b, runlength in run_length_encode(space)],
            color_map=color_map_fn,
            out_fname=f"frag_{fname_postfix}.png",
            vpad=4,

            )

def plot_alloc_patterns(space, obj_by_id, allocator, actions, fname_postfix):
    def to_blk_cnt(sz):
        return (sz // BLK_SZ) + (1 if sz % BLK_SZ else 0)

    sns_colors = sns.color_palette("mako")

    color_map_fn = create_alternating_color_map({
        True:  [sns_colors[0], sns_colors[1]],  # for is_delete_action
        False: [sns_colors[-3], sns_colors[-2]], # for not is_delete_action
        })

    plot_run_lengths(
            runs=[(act.is_delete_action, to_blk_cnt(obj_by_id[act.obj_id].data_sz)) for act in actions],
            color_map=color_map_fn,
            out_fname=f"alloc_{fname_postfix}.png",
            vpad=4,
            )

def create_alternating_color_map(multi_color_map):
    if not isinstance(multi_color_map, dict):
        raise TypeError()

    last_seen = None
    ix = 0
    def color_map_fn(key):
        nonlocal ix
        nonlocal last_seen
        if key == last_seen:
            ix += 1
        else:
            ix = 0

        # so the next call knows that what was the previous
        # action
        last_seen = key
        colors = multi_color_map[key]

        # iterate over the colors for that key
        #
        # multi_color_map *must* be a dict of lists of (rgb) colors
        return colors[ix % len(colors)]

    return color_map_fn

def ensure_uint8_color(color):
    # If [0 1) color coordinates are used, transform them
    # to range [0 256)
    if any(isinstance(c, float) for c in color):
        return tuple(round(c * 255) for c in color)
    return color

def plot_run_lengths(runs, color_map, pad_color=(0,0,0), height_line=16, width_line=4, vpad=1.0, total_length=None, out_fname=None):
    pad_color = ensure_uint8_color(pad_color)

    H = height_line
    W = width_line

    if isinstance(vpad, float):
        VPAD = round(vpad * H)
    else:
        VPAD = vpad

    if total_length is None:
        total_length = sum(runlength for _, runlength in runs)

    cols_data = round( ((total_length / 1.61)**0.5) * H/W )
    rows_data = (total_length // cols_data) + (1 if total_length % cols_data else 0)

    img_width = cols_data * W
    img_height = rows_data * H + (rows_data - 1) * VPAD


    img = PIL.Image.new(
            size=(img_width, img_height),
            mode="RGB"
            )
    draw = ImageDraw.Draw(img)

    color_map_is_a_dict = isinstance(color_map, dict)

    col = x = y = 0
    for code, runlength in runs:
        if color_map_is_a_dict:
            color = color_map[code]
        else:
            color = color_map(code)

        color = ensure_uint8_color(color)

        while runlength > 0:
            # If the runlength is too large, we will have to
            # draw it into multiple lines.
            #
            # For now, focus on the first line.
            longest_run_within_line = min(runlength, cols_data - col)
            L = longest_run_within_line # alias

            draw.rectangle([
                (x,     y),         # (x0, y0) inclusive
                (x+(W*L)-1, y+H-1)  # (x1, y1) inclusive
                ],
                fill=color,
                outline=color
                )

            x += L * W
            col += L

            # If more lines are required we need to draw the padding
            # line before continuing
            if longest_run_within_line < runlength:
                assert x >= img_width
                assert col >= cols_data

                # padding line
                col = 0
                x = 0
                y += H

                # draw only if padding was requested
                if VPAD > 0:
                    draw.rectangle([
                        (0,           y),        # (x0, y0) inclusive
                        (img_width-1, y+VPAD-1)  # (x1, y1) inclusive
                        ],
                        fill=pad_color,
                        outline=pad_color
                        )

                y += VPAD

            # update current runlength
            runlength -= longest_run_within_line

    if out_fname is not None:
        img.save(out_fname)
    return img

def show_space_pages(space, obj_by_id, allocator, fname_postfix):
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

def show_space_stats(space, obj_by_id, allocator, fname_postfix):
    total_data_sz = sum(obj.data_sz for obj in obj_by_id.values() if obj.segm is not None)

    total_blk_cnt = len(space)
    non_free_blk_cnt = sum(1 if b != 0 else 0 for b in space)

    free_blk_cnt = total_blk_cnt - non_free_blk_cnt
    free_blk_at_end_cnt = sum(1 for _ in itertools.takewhile(lambda b: b == 0, reversed(space)))

    total_file_sz = total_blk_cnt * BLK_SZ

    internal_frag_sz = (non_free_blk_cnt * BLK_SZ) - total_data_sz
    assert internal_frag_sz >= 0


    print("Block cnt:", total_blk_cnt)
    print("File size:", kb(total_file_sz), "kb")
    print("Useful data size:", kb(total_data_sz), "kb")
    print("Minimum blk required (theoretical perfect case):", (total_data_sz // BLK_SZ) + 1)
    print()

    print("Free block cnt:", free_blk_cnt)
    print("Free block (at the end) cnt:", free_blk_at_end_cnt)
    print("Free size:", kb(free_blk_cnt * BLK_SZ), "kb")
    print()
    print("External frag:", round((free_blk_cnt / total_blk_cnt) * 100, 2), "% of blocks are freed/unused")
    print("Internal frag:", round((internal_frag_sz / total_data_sz) * 100, 2), "% of data is reserved but wasted (doesn't contain useful data)")
    print()

    print("Object frag - quantiles of extent count per object:")
    obj_ext_counts_df = pd.DataFrame(
            [len(obj.segm.extents) for obj in obj_by_id.values() if obj.segm is not None],
            columns=["Extent count"]
            )
    print(obj_ext_counts_df.quantile([.5, .9, .99, 1]))
    print()

#SAMPLE_TARGET = 'ph-01'
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
@click.option('-a', '--allocator', type=click.Choice(['mono', 'kr', 'halving-kr', 'linear-kr']), required=True)
@click.option('-s', '--sample', type=click.Choice(_samples), default='ph-01')
@click.option('--coalescing', is_flag=True, default=False)
@click.option('-m', '--writer-model', type=click.Choice(['copier', 'notetaker', 'editor']), default='editor')
@click.option('--no-reinsert', is_flag=True, default=False)
@click.option('--contract/--no-contract', default=True)
@click.option('--trace/--no-trace', default=False)
@click.option('--show-progress/--no-show-progress', default=True)
@click.option('--show-map', type=click.Choice(['pages', 'types', 'objs', 'img-types', 'img-frag', 'img-alloc']), multiple=True, default=[])
@click.option('--max-ext-cnt', default=8)
@click.option('--min-fr-split-remain', default=1)
@click.option('--fname-tag', default="")
def main(seed, rerun_until_bug, note_taker_back_w, dp, idp, rf, allocator, sample, coalescing, writer_model, no_reinsert, contract, trace, show_progress, show_map, max_ext_cnt, min_fr_split_remain, fname_tag):
    SEED = seed
    NOTE_TAKER_BACK_W = max(4, note_taker_back_w)
    DEL_PROB = min(0.9, max(0, dp))
    DEL_IMG_PROB = min(0.9, max(0, idp))
    REINSERT_CHG_SZ_FACTOR = max(0.001, rf)
    ALLOCATOR = allocator
    SAMPLE_TARGET = _destacated_samples.get(sample, sample)
    COALESCING = coalescing
    WRITER_MODEL = writer_model
    REINSERT = not no_reinsert
    CONTRACT = contract
    TRACE = trace
    NOT_SHOW_PROGRESS = not show_progress
    SHOW_MAPS = show_map
    MAX_EXTENT_CNT = max_ext_cnt
    MIN_FR_SPLIT_REMAIN = min_fr_split_remain
    FNAME_TAG = fname_tag

    df = pd.read_csv('01-results/xopp-dataset-2023.csv')

    df2 = df[df['sample'] == SAMPLE_TARGET]
    sample_df = df2[['sample', 'type', 'char_length', 'widths_cnt', 'coords_cnt', 'text', 'page']]

    # Collect all the "draw" actions and objects
    _main_actions = []
    _main_obj_by_id = {}
    obj_id = 0
    for _, row in tqdm.tqdm(sample_df.iterrows(), total=len(sample_df), disable=NOT_SHOW_PROGRESS):
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
                NOT_SHOW_PROGRESS = NOT_SHOW_PROGRESS,
                SHOW_MAPS = SHOW_MAPS,
                MAX_EXTENT_CNT = MAX_EXTENT_CNT,
                SAMPLE_TARGET = SAMPLE_TARGET,
                MIN_FR_SPLIT_REMAIN = MIN_FR_SPLIT_REMAIN,
                FNAME_TAG = FNAME_TAG
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
        NOT_SHOW_PROGRESS,
        SHOW_MAPS,
        MAX_EXTENT_CNT,
        SAMPLE_TARGET,
        MIN_FR_SPLIT_REMAIN,
        FNAME_TAG
    ):

    def bool2short(b):
        return "yes" if b else "no"

    fname_postfix = f"sample:{SAMPLE_TARGET}_alloc:{ALLOCATOR}_coal:{bool2short(COALESCING)}_contr:{bool2short(CONTRACT)}_maxext:{MAX_EXTENT_CNT}_minsplit:{MIN_FR_SPLIT_REMAIN}_model:{WRITER_MODEL}_back:{NOTE_TAKER_BACK_W}_rmprobs:{DEL_PROB}_{DEL_IMG_PROB}_reins:{bool2short(REINSERT)}_{REINSERT_CHG_SZ_FACTOR}_seed:{SEED}"
    if FNAME_TAG:
        fname_postfix += f"_tag:{FNAME_TAG}"

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
    T = tqdm.tqdm(disable=NOT_SHOW_PROGRESS)
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
        obj_by_id = copy.deepcopy(_main_obj_by_id)

        if ALLOCATOR == 'mono':
            allocator = MonotonicAllocator()
        elif ALLOCATOR == 'kr':
            allocator = KRAllocator(coalescing=COALESCING, min_fr_split_remain=MIN_FR_SPLIT_REMAIN)
        elif ALLOCATOR == 'halving-kr':
            allocator = HalvingAllocator(KRAllocator(coalescing=COALESCING, min_fr_split_remain=MIN_FR_SPLIT_REMAIN), None)
        elif ALLOCATOR == 'linear-kr':
            allocator = LinearAllocator(KRAllocator(coalescing=COALESCING, min_fr_split_remain=MIN_FR_SPLIT_REMAIN), MAX_EXTENT_CNT)
        else:
            assert False

        sim = Simulator(allocator, space, obj_by_id, trace_enabled=TRACE)
        #plot_alloc_patterns(space, obj_by_id, allocator, actions, fname_postfix)
        #return

        TA = tqdm.tqdm(total=sum(1 for act in actions if not act.is_delete_action), position=1, disable=TRACE or NOT_SHOW_PROGRESS)
        TD = tqdm.tqdm(total=sum(1 for act in actions if act.is_delete_action), position=2, disable=TRACE or NOT_SHOW_PROGRESS)
        for act in tqdm.tqdm(actions, total=len(actions), position=0, disable=TRACE or NOT_SHOW_PROGRESS):
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
        for show_map_name in sorted(set(SHOW_MAPS)):
            if show_map_name == 'pages':
                show_space_pages(space, obj_by_id, allocator, fname_postfix)
            elif show_map_name == 'objs':
                show_space_obj_ids(space, obj_by_id, allocator, fname_postfix)
            elif show_map_name == 'types':
                show_space_obj_types(space, obj_by_id, allocator, fname_postfix)
            elif show_map_name == 'img-types':
                img_space_obj_types(space, obj_by_id, allocator, fname_postfix)
            elif show_map_name == 'img-frag':
                img_space_frag(space, obj_by_id, allocator, fname_postfix)
            elif show_map_name == 'img-alloc':
                plot_alloc_patterns(space, obj_by_id, allocator, actions, fname_postfix)
            else:
                assert False

        show_space_stats(space, obj_by_id, allocator, fname_postfix)

if __name__ == '__main__':
    import sys
    main()
