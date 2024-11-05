// Copyright 2022, University of Freiburg
// Chair of Algorithms and Data Structures
// Author: Hannah Bast <bast@cs.uni-freiburg.de>

#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "absl/container/node_hash_set.h"
#include "global/Id.h"
#include "parser/LiteralOrIri.h"
#include "util/AllocatorWithLimit.h"
#include "util/BlankNodeManager.h"
#include "util/HashSet.h"
#include "util/MemorySize/MemorySize.h"

// A class for maintaining a local vocabulary with contiguous (local) IDs. This
// is meant for words that are not part of the normal vocabulary (constructed
// from the input data at indexing time).
//

class LocalVocab {
 private:
  using Entry = LocalVocabEntry;
  using LiteralOrIri = LocalVocabEntry;

  // A functor that calculates the memory size of an IRI or Literal.
  // This struct defines an operator() that takes a `LiteralOrIri` object and
  // returns its dynamic memory usage in bytes.
  struct IriSizeGetter {
    ad_utility::MemorySize operator()(
        const ad_utility::triple_component::LiteralOrIri& literalOrIri) {
      return ad_utility::MemorySize::bytes(
          literalOrIri.getDynamicMemoryUsage());
    }
  };

  // A map of the words in the local vocabulary to their local IDs. This is a
  // node hash map because we need the addresses of the words (which are of type
  // `LiteralOrIri`) to remain stable over their lifetime in the hash map
  // because we hand out pointers to them.
  using Set =
      ad_utility::CustomHashSetWithMemoryLimit<LiteralOrIri, IriSizeGetter>;
  ad_utility::detail::AllocationMemoryLeftThreadsafe limit_;
  std::shared_ptr<Set> primaryWordSet_;

  IriSizeGetter sizeGetter;

  // Local vocabularies from child operations that were merged into this
  // vocabulary s.t. the pointers are kept alive. They have to be `const`
  // because they are possibly shared concurrently (for example via the cache).
  std::vector<std::shared_ptr<const Set>> otherWordSets_;

  auto& primaryWordSet() { return *primaryWordSet_; }
  const auto& primaryWordSet() const { return *primaryWordSet_; }

  std::optional<ad_utility::BlankNodeManager::LocalBlankNodeManager>
      localBlankNodeManager_;

 public:
  // Create a new, empty local vocabulary.
  LocalVocab(ad_utility::detail::AllocationMemoryLeftThreadsafe memoryLimit =
                 ad_utility::makeAllocationMemoryLeftThreadsafeObject(
                     ad_utility::MemorySize::megabytes(100)))
      : limit_(memoryLimit),
        primaryWordSet_(std::make_shared<Set>(limit_, sizeGetter)) {}

  // Prevent accidental copying of a local vocabulary.
  LocalVocab(const LocalVocab&) = delete;
  LocalVocab& operator=(const LocalVocab&) = delete;

  // Make a logical copy. The clone will have an empty primary set so it can
  // safely be modified. The contents are copied as shared pointers to const, so
  // the function runs in linear time in the number of word sets.
  LocalVocab clone() const;

  // Moving a local vocabulary is not problematic (though the typical use case
  // in our code is to copy shared pointers to local vocabularies).
  LocalVocab(LocalVocab&&) = default;
  LocalVocab& operator=(LocalVocab&&) = default;

  // Get the index of a word in the local vocabulary. If the word was already
  // contained, return the already existing index. If the word was not yet
  // contained, add it, and return the new index.
  LocalVocabIndex getIndexAndAddIfNotContained(const LiteralOrIri& word);
  LocalVocabIndex getIndexAndAddIfNotContained(LiteralOrIri&& word);

  // Get the index of a word in the local vocabulary, or std::nullopt if it is
  // not contained. This is useful for testing.
  std::optional<LocalVocabIndex> getIndexOrNullopt(
      const LiteralOrIri& word) const;

  // The number of words in the vocabulary.
  // Note: This is not constant time, but linear in the number of word sets.
  size_t size() const {
    auto result = primaryWordSet().size();
    for (const auto& previous : otherWordSets_) {
      result += previous->size();
    }
    return result;
  }

  // Return true if and only if the local vocabulary is empty.
  bool empty() const { return size() == 0; }

  // Return a const reference to the word.
  const LiteralOrIri& getWord(LocalVocabIndex localVocabIndex) const;

  // Create a local vocab that contains and keeps alive all the words from each
  // of the `vocabs`. The primary word set of the newly created vocab is empty.
  static LocalVocab merge(std::span<const LocalVocab*> vocabs);

  // Merge all passed local vocabs to keep alive all the words from each of the
  // `vocabs`.
  template <std::ranges::range R>
  void mergeWith(const R& vocabs) {
    auto inserter = std::back_inserter(otherWordSets_);
    for (const auto& vocab : vocabs) {
      std::ranges::copy(vocab.otherWordSets_, inserter);
      *inserter = vocab.primaryWordSet_;
    }
  }

  // Return all the words from all the word sets as a vector.
  std::vector<LiteralOrIri> getAllWordsForTesting() const;

  // Get a new BlankNodeIndex using the LocalBlankNodeManager.
  [[nodiscard]] BlankNodeIndex getBlankNodeIndex(
      ad_utility::BlankNodeManager* blankNodeManager);

  // Return true iff the given `blankNodeIndex` is one that was previously
  // generated by the blank node manager of this local vocab.
  bool isBlankNodeIndexContained(BlankNodeIndex blankNodeIndex) const;

 private:
  // Common implementation for the two variants of
  // `getIndexAndAddIfNotContainedImpl` above.
  template <typename WordT>
  LocalVocabIndex getIndexAndAddIfNotContainedImpl(WordT&& word);
};
