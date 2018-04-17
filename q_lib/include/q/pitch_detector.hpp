/*=============================================================================
   Copyright (c) 2014-2018 Joel de Guzman. All rights reserved.

   Distributed under the Boost Software License, Version 1.0. (See accompanying
   file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
=============================================================================*/
#if !defined(CYCFI_Q_PITCH_DETECTOR_HPP_MARCH_12_2018)
#define CYCFI_Q_PITCH_DETECTOR_HPP_MARCH_12_2018

#include <q/bacf.hpp>

namespace cycfi { namespace q
{
   ////////////////////////////////////////////////////////////////////////////
   template <typename T = std::uint32_t>
   class pitch_detector
   {
   public:
                              pitch_detector(
                                 frequency lowest_freq
                               , frequency highest_freq
                               , std::uint32_t sps
                              );

                              pitch_detector(pitch_detector const& rhs) = default;
                              pitch_detector(pitch_detector&& rhs) = default;

      pitch_detector&         operator=(pitch_detector const& rhs) = default;
      pitch_detector&         operator=(pitch_detector&& rhs) = default;

      bool                    operator()(float s);
      bacf<T> const&          bacf() const         { return _bacf; }
      float                   frequency() const    { return _frequency; }

   private:

      std::size_t             index() const;
      float                   calculate_frequency(std::size_t edge) const;

      using signal_iterator = typename std::vector<float>::iterator;

      q::bacf<T>              _bacf;
      std::vector<float>      _signal;
      float                   _frequency;
      std::uint32_t           _sps;
      window_comparator       _cmp{ -0.2f, 0.0f };
      std::size_t             _ticks = 0;
      float                   _max_val = 0.0f;
   };

   ////////////////////////////////////////////////////////////////////////////
   // Implementation
   ////////////////////////////////////////////////////////////////////////////
   template <typename T>
   inline pitch_detector<T>::pitch_detector(
       q::frequency lowest_freq
     , q::frequency highest_freq
     , std::uint32_t sps
   )
     : _bacf(lowest_freq, highest_freq, sps)
     , _signal(_bacf.size(), 0.0f)
     , _frequency(0.0f)
     , _sps(sps)
   {}

   template <typename T>
   inline bool pitch_detector<T>::operator()(float s)
   {
      // Save signal
      _signal[_ticks++] = s;
      if (_max_val < s) // Get the maxima; Positive only!
         _max_val = s;

      bool proc = false;
      if (_ticks == _bacf.size())
      {
         if (_max_val > 0.005) // noise gate
         {
            auto norm = 1.0 / _max_val;
            auto first_low = false;
            auto edge = -1;

            for (auto i = _signal.begin(); i != _signal.end(); ++i)
            {
               // Normalization
               auto& s = *i;
               s *= norm;
               if (s < -1.0f)
                  s = -1.0f;

               // Bitstream-ize
               auto b = _cmp(s);

               // Get the first edge index
               if (!b && !first_low)
                  first_low = true;
               else if (b && first_low && edge == -1)
                  edge = i - _signal.begin();

               // Correlation
               proc = _bacf(b);

               // Compute Frequency
               if (proc)
               {
                  auto f = calculate_frequency(edge);
                  if (f != 0)
                     _frequency = f;
               }
            }
         }

         // cycle the signal buffer
         _ticks = 0;
         _max_val = 0.0f; // clear normalization max
      }
      return proc;
   }

   namespace detail
   {
      template <std::size_t harmonic>
      struct find_harmonics
      {
         template <typename Correlation>
         static std::size_t
         call(
            Correlation const& corr, std::size_t index
          , std::size_t min_period, float threshold
         )
         {
            auto delta = index / harmonic;
            if (delta < min_period)
               return index;

            for (auto i = delta; i < index; i += delta)
               if (corr[i] > threshold)
                  return find_harmonics<harmonic+1>::call(
                     corr, index, min_period, threshold);
            return delta;
         }
      };

      template <>
      struct find_harmonics<5>
      {
         template <typename Correlation>
         static std::size_t
         call(
            Correlation const& corr, std::size_t index
          , std::size_t min_period, float threshold
         )
         {
            return index;
         }
      };
   }

   template <typename T>
   inline std::size_t pitch_detector<T>::index() const
   {
      auto const& info = _bacf.result();
      if (info.max_count == info.min_count)
         return 0;
      auto const& corr = info.correlation;
      auto index = info.index;
      auto threshold = 0.15 * info.max_count;
      auto min_period = _bacf.minimum_period();
      auto found = detail::find_harmonics<2>::call(corr, index, min_period, threshold);
      if (corr[found] > threshold)
         return 0;
      return found;
   }

   template <typename T>
   float pitch_detector<T>::calculate_frequency(std::size_t edge) const
   {
      auto pos = index();
      if (pos == 0)
         return 0.0f;

      // Get the start edge
      auto prev1 = _signal[edge - 1];
      auto curr1 = _signal[edge];
      auto dy1 = curr1 - prev1;
      auto dx1 = -prev1 / dy1;

      // Get the next edge
      auto next = edge + pos - 1;
      while (_signal[next] > 0.0f)
         --next;
      auto prev2 = _signal[next++];
      auto curr2 = _signal[next];
      auto dy2 = curr2 - prev2;
      auto dx2 = -prev2 / dy2;

      // Calculate the frequency
      float n_samples = (next - edge) + (dx2 - dx1);
      return _sps / n_samples;
   }
}}

#endif

