#pragma once

#include "launch.hpp"

namespace tf {

// Function: make_transform_task
template <
  typename B, typename E, typename O, typename C, typename P = GuidedPartitioner, typename TW = TaskWrapperIdent,
  std::enable_if_t<is_partitioner_v<std::decay_t<P>>, void>* = nullptr
>
TF_FORCE_INLINE auto make_transform_task(
  B first1, E last1, O d_first, C c, P&& part = P(), TW&&task_wrapper = TW()
) {

  using namespace std::string_literals;

  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;
  using O_t = std::decay_t<unwrap_ref_decay_t<O>>;
  
  return
  [first1, last1, d_first, c, part=std::forward<P>(part), task_wrapper=std::forward<TW>(task_wrapper)] 
  (Runtime& rt) mutable {

    // fetch the stateful values
    B_t beg   = first1;
    E_t end   = last1;
    O_t d_beg = d_first;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg, end);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      std::transform(beg, end, d_beg, c);
      return;
    }

    if(N < W) {
      W = N;
    }

    // static partitioner
    if constexpr(std::is_same_v<std::decay_t<P>, StaticPartitioner>) {
      size_t chunk_size;
      for(size_t w=0, curr_b=0; w<W && curr_b < N; ++w, curr_b += chunk_size) {
        chunk_size = part.adjusted_chunk_size(N, W, w);
        launch_loop(W, w, rt, [=, &part] () mutable {
          task_wrapper([&]{
            part.loop(N, W, curr_b, chunk_size,
              [&, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
                std::advance(beg, part_b - prev_e);
                std::advance(d_beg, part_b - prev_e);
                for(size_t x = part_b; x<part_e; x++) {
                  *d_beg++ = c(*beg++);
                }
                prev_e = part_e;
              }
            ); 
          });
        });
      }
      rt.corun_all();
    }
    // dynamic partitioner
    else {
      std::atomic<size_t> next(0);
      
      launch_loop(N, W, rt, next, part, [=, &next, &part] () mutable {
        task_wrapper([&]{
          part.loop(N, W, next, 
            [&, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
              std::advance(beg, part_b - prev_e);
              std::advance(d_beg, part_b - prev_e);
              for(size_t x = part_b; x<part_e; x++) {
                *d_beg++ = c(*beg++);
              }
              prev_e = part_e;
            }
          ); 
        });
      });
    }
  };
}

// Function: make_transform_task
template <
  typename B1, typename E1, typename B2, typename O, typename C, typename P = GuidedPartitioner, typename TW = TaskWrapperIdent,
  std::enable_if_t<!is_partitioner_v<std::decay_t<C>>, void>* = nullptr
>
TF_FORCE_INLINE auto make_transform_task(
  B1 first1, E1 last1, B2 first2, O d_first, C c, P&& part = P(), TW&&task_wrapper = TW()
) {

  using namespace std::string_literals;

  using B1_t = std::decay_t<unwrap_ref_decay_t<B1>>;
  using E1_t = std::decay_t<unwrap_ref_decay_t<E1>>;
  using B2_t = std::decay_t<unwrap_ref_decay_t<B2>>;
  using O_t = std::decay_t<unwrap_ref_decay_t<O>>;

  return
  [first1, last1, first2, d_first, c, part=std::forward<P>(part),task_wrapper=std::forward<TW>(task_wrapper)] 
  (Runtime& rt) mutable {

    // fetch the stateful values
    B1_t beg1 = first1;
    E1_t end1 = last1;
    B2_t beg2 = first2;
    O_t d_beg = d_first;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg1, end1);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      std::transform(beg1, end1, beg2, d_beg, c);
      return;
    }

    if(N < W) {
      W = N;
    }

    // static partitioner
    if constexpr(std::is_same_v<std::decay_t<P>, StaticPartitioner>) {
      size_t chunk_size;
      for(size_t w=0, curr_b=0; w<W && curr_b < N; ++w, curr_b += chunk_size) {
        chunk_size = part.adjusted_chunk_size(N, W, w);
        launch_loop(W, w, rt, [=, &c, &part] () mutable {
          task_wrapper([&]{
            part.loop(N, W, curr_b, chunk_size,
              [&, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
                std::advance(beg1, part_b - prev_e);
                std::advance(beg2, part_b - prev_e);
                std::advance(d_beg, part_b - prev_e);
                for(size_t x = part_b; x<part_e; x++) {
                  *d_beg++ = c(*beg1++, *beg2++);
                }
                prev_e = part_e;
              }
            ); 
          });
        });
      }
      rt.corun_all();
    }
    // dynamic partitioner
    else {
      std::atomic<size_t> next(0);
      launch_loop(N, W, rt, next, part, [=, &c, &next, &part] () mutable {
        task_wrapper([&]{
          part.loop(N, W, next, 
            [&, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
              std::advance(beg1, part_b - prev_e);
              std::advance(beg2, part_b - prev_e);
              std::advance(d_beg, part_b - prev_e);
              for(size_t x = part_b; x<part_e; x++) {
                *d_beg++ = c(*beg1++, *beg2++);
              }
              prev_e = part_e;
            }
          ); 
        });
      });
    }
  };
}

// ----------------------------------------------------------------------------
// transform
// ----------------------------------------------------------------------------

// Function: transform
template <typename B, typename E, typename O, typename C, typename P, typename W,
  std::enable_if_t<is_partitioner_v<std::decay_t<P>>, void>*
>
Task FlowBuilder::transform(B first1, E last1, O d_first, C c, P&& part, W&&task_wrapper) {
  return emplace(
    make_transform_task(first1, last1, d_first, c, std::forward<P>(part), std::forward<W>(task_wrapper))
  );
}

// ----------------------------------------------------------------------------
// transform2
// ----------------------------------------------------------------------------
  
// Function: transform
template <
  typename B1, typename E1, typename B2, typename O, typename C, typename P, typename W,
  std::enable_if_t<!is_partitioner_v<std::decay_t<C>>, void>*
>
Task FlowBuilder::transform(
  B1 first1, E1 last1, B2 first2, O d_first, C c, P&& part, W && task_wrapper
) {
  return emplace(make_transform_task(
    first1, last1, first2, d_first, c, std::forward<P>(part), std::forward<W>(task_wrapper)
  ));
}


}  // end of namespace tf -----------------------------------------------------



