#include "utils/hashtable.hpp"

#define XXH_INLINE_ALL

#include <xmmintrin.h>
#include "thirdparty/xxhash.h"
#include <iostream>

HashTable::HashTable(std::size_t key_size, std::size_t value_size) :
    m_key_size(key_size), m_value_size(value_size)
{
    init_blocks();
}

void
HashTable::init_blocks()
{
    HashTableBlock zeroed_block;
    memset(&zeroed_block, 0, sizeof(zeroed_block));
    for (int i = 0; i < 16; i++) {
        zeroed_block.tags[i] = 0x80;
    }
    m_blocks.resize(m_block_count);
    for (auto &block : m_blocks) {
        block = zeroed_block;
    }
}


bool
HashTable::lookup(uint8_t *key, uint8_t *&item, bool create_if_not_found)
{
    uint64_t hash = XXH3_64bits(key, m_key_size);
    uint64_t index = (hash >> 7) & (m_block_count - 1);

    //if (m_debug) printf("Hash is %lu\n", hash);

    //index = 42;
    for (;;) {
        //std::cout << "Index=" << index << std::endl;

        HashTableBlock &block = m_blocks[index];

        uint8_t item_tag = (hash & 0xFF) & 0x7F;
        auto block_tags = _mm_load_si128(reinterpret_cast<__m128i *>(block.tags));
        auto hash_mask = _mm_set1_epi8(item_tag);
        auto empty_mask = _mm_set1_epi8(0x80);

        auto hash_match = _mm_movemask_epi8(_mm_cmpeq_epi8(block_tags, hash_mask));
        auto empty_match = _mm_movemask_epi8(_mm_cmpeq_epi8(block_tags, empty_mask));

        int item_index = 0;
        while (hash_match) {
            //if (m_debug) std::cout << "Hash match=" << hash_match << std::endl;
            auto one_index = __builtin_ctz(hash_match);
            item_index += one_index;
            //if (m_debug) std::cout << "One index=" << one_index << " Item index=" << item_index << std::endl;

            uint8_t *record = block.items[item_index];
            if (memcmp(record, key, m_key_size) == 0) {
                //if (m_debug) std::cout << "Found key on " << index << ":" << item_index << std::endl;
                item = record;
                return true;
            }

            hash_match >>= one_index + 1;
            item_index += 1;
        }

        if (empty_match) {

            if (!create_if_not_found) {
                //if (m_debug) printf("Not found\n");
                return false;
            }

            //std::cout << "Empty match=" << empty_match << std::endl;
            auto empty_index = __builtin_ctz(empty_match);
            //std::cout << "Found empty index on " << index << ":" << empty_index << std::endl;
            block.tags[empty_index] = item_tag;

            uint8_t *record = new uint8_t[m_key_size + m_value_size];
            block.items[empty_index] = record;
            m_items.push_back(record);
            m_record_count++;

            //std::cout << "Calloc done" << std::endl;
            memcpy(record, key, m_key_size);
            //std::cout << "Memcpy done" << std::endl;
            item = record;

            if (double(m_record_count) / (16 * double(m_block_count)) > 0.9) {
                expand();
            }

            return false;
        }

        index = (index + 1) & (m_block_count - 1);
    }
}

void
HashTable::expand()
{
    m_block_count *= 4;
    //std::cout << "Expand to " << m_block_count << std::endl;
    init_blocks();
    for (uint8_t *item : m_items) {
        uint64_t hash = XXH3_64bits(item, m_key_size);
        uint64_t index = (hash >> 7) & (m_block_count - 1);
        uint8_t item_tag = (hash & 0xFF) & 0x7F;

        for (;;) {
            HashTableBlock &block = m_blocks[index];

            auto block_tags = _mm_load_si128(reinterpret_cast<__m128i *>(block.tags));
            auto empty_mask = _mm_set1_epi8(0x80);
            auto empty_match = _mm_movemask_epi8(_mm_cmpeq_epi8(block_tags, empty_mask));
            if (empty_match) {
                //std::cout << "Empty match=" << empty_match << std::endl;
                auto empty_index = __builtin_ctz(empty_match);
                //std::cout << "Found empty index on " << index << ":" << empty_index << std::endl;
                block.tags[empty_index] = item_tag;
                block.items[empty_index] = item;
                break;
            }

            index = (index + 1) & (m_block_count - 1);
        }

    }
}

bool
HashTable::find(uint8_t *key, uint8_t *&item)
{
    return lookup(key, item, false);
}

bool
HashTable::find_or_create(uint8_t *key, uint8_t *&item)
{
    return lookup(key, item, true);
}