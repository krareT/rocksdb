//
// Created by leipeng on 2020/7/1.
//
#include "table/block_based/block_based_table_factory.h"
#include "table/plain/plain_table_factory.h"
#include "table/table_builder.h"
#include "json.h"
#include "json_plugin_factory.h"

namespace ROCKSDB_NAMESPACE {

static std::shared_ptr<const FilterPolicy>
NewBloomFilterPolicyJson(const json& js, const JsonOptionsRepo&, Status*) {
  double bits_per_key = 10;
  bool use_block_based_builder = false;
  ROCKSDB_JSON_OPT_PROP(js, bits_per_key);
  ROCKSDB_JSON_OPT_PROP(js, use_block_based_builder);
  return std::shared_ptr<const FilterPolicy>(
      NewBloomFilterPolicy(bits_per_key, use_block_based_builder));
}
ROCKSDB_FACTORY_REG("BloomFilter", NewBloomFilterPolicyJson);

struct BlockBasedTableOptions_Json : BlockBasedTableOptions {
  Status UpdateFromJson(const json& js, const JsonOptionsRepo& repo) {
    try {
      ROCKSDB_JSON_OPT_FACT(js, flush_block_policy_factory);
      ROCKSDB_JSON_OPT_PROP(js, cache_index_and_filter_blocks);
      ROCKSDB_JSON_OPT_PROP(js, cache_index_and_filter_blocks_with_high_priority);
      ROCKSDB_JSON_OPT_PROP(js, pin_l0_filter_and_index_blocks_in_cache);
      ROCKSDB_JSON_OPT_PROP(js, pin_top_level_index_and_filter);
      ROCKSDB_JSON_OPT_PROP(js, pin_l0_filter_and_index_blocks_in_cache);
      ROCKSDB_JSON_OPT_ENUM(js, index_type);
      ROCKSDB_JSON_OPT_ENUM(js, data_block_index_type);
      ROCKSDB_JSON_OPT_ENUM(js, index_shortening);
      ROCKSDB_JSON_OPT_ENUM(js, data_block_index_type);
      ROCKSDB_JSON_OPT_PROP(js, data_block_hash_table_util_ratio);
      ROCKSDB_JSON_OPT_PROP(js, hash_index_allow_collision);
      ROCKSDB_JSON_OPT_ENUM(js, checksum);
      ROCKSDB_JSON_OPT_PROP(js, no_block_cache);
      ROCKSDB_JSON_OPT_PROP(js, block_size);
      ROCKSDB_JSON_OPT_PROP(js, block_size_deviation);
      ROCKSDB_JSON_OPT_PROP(js, block_restart_interval);
      ROCKSDB_JSON_OPT_PROP(js, index_block_restart_interval);
      ROCKSDB_JSON_OPT_PROP(js, metadata_block_size);
      ROCKSDB_JSON_OPT_PROP(js, partition_filters);
      ROCKSDB_JSON_OPT_PROP(js, use_delta_encoding);
      ROCKSDB_JSON_OPT_PROP(js, read_amp_bytes_per_bit);
      ROCKSDB_JSON_OPT_PROP(js, whole_key_filtering);
      ROCKSDB_JSON_OPT_PROP(js, verify_compression);
      ROCKSDB_JSON_OPT_PROP(js, format_version);
      ROCKSDB_JSON_OPT_PROP(js, enable_index_compression);
      ROCKSDB_JSON_OPT_PROP(js, block_align);
      ROCKSDB_JSON_OPT_FACT(js, block_cache);
      ROCKSDB_JSON_OPT_FACT(js, block_cache_compressed);
      ROCKSDB_JSON_OPT_FACT(js, persistent_cache);
      ROCKSDB_JSON_OPT_FACT(js, filter_policy);
      return Status::OK();
    } catch (const std::exception& ex) {
      return Status::InvalidArgument(ROCKSDB_FUNC, ex.what());
    }
  }
};

#if 0
std::string BlockBasedTableFactory::GetOptionJson() const {
  std::string ret;
  ret.reserve(20000);
  const int kBufferSize = 200;
  char buffer[kBufferSize];

  ret.append("{");
  snprintf(buffer, kBufferSize, "  flush_block_policy_factory: %s, ",
           table_options_.flush_block_policy_factory->Name());
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  cache_index_and_filter_blocks: %d, ",
           table_options_.cache_index_and_filter_blocks);
  ret.append(buffer);
  snprintf(buffer, kBufferSize,
           "  cache_index_and_filter_blocks_with_high_priority: %d, ",
           table_options_.cache_index_and_filter_blocks_with_high_priority);
  ret.append(buffer);
  snprintf(buffer, kBufferSize,
           "  pin_l0_filter_and_index_blocks_in_cache: %d, ",
           table_options_.pin_l0_filter_and_index_blocks_in_cache);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  pin_top_level_index_and_filter: %d, ",
           table_options_.pin_top_level_index_and_filter);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  index_type: %d, ",
           table_options_.index_type);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  data_block_index_type: %d, ",
           table_options_.data_block_index_type);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  index_shortening: %d, ",
           static_cast<int>(table_options_.index_shortening));
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  data_block_hash_table_util_ratio: %lf, ",
           table_options_.data_block_hash_table_util_ratio);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  hash_index_allow_collision: %d, ",
           table_options_.hash_index_allow_collision);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  checksum: %d, ", table_options_.checksum);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  no_block_cache: %d, ",
           table_options_.no_block_cache);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_cache: %p, ",
           static_cast<void*>(table_options_.block_cache.get()));
  ret.append(buffer);
/*
  if (table_options_.block_cache) {
    const char* block_cache_name = table_options_.block_cache->Name();
    if (block_cache_name != nullptr) {
      snprintf(buffer, kBufferSize, "  block_cache_name: %s, ",
               block_cache_name);
      ret.append(buffer);
    }
    ret.append("  block_cache_options:, ");
    ret.append(table_options_.block_cache->GetOptionJson());
  }
  snprintf(buffer, kBufferSize, "  block_cache_compressed: %p, ",
           static_cast<void*>(table_options_.block_cache_compressed.get()));
  ret.append(buffer);
  if (table_options_.block_cache_compressed) {
    const char* block_cache_compressed_name =
        table_options_.block_cache_compressed->Name();
    if (block_cache_compressed_name != nullptr) {
      snprintf(buffer, kBufferSize, "  block_cache_name: %s, ",
               block_cache_compressed_name);
      ret.append(buffer);
    }
    ret.append("  block_cache_compressed_options:, ");
    ret.append(table_options_.block_cache_compressed->GetOptionJson());
  }
  snprintf(buffer, kBufferSize, "  persistent_cache: %p, ",
           static_cast<void*>(table_options_.persistent_cache.get()));
  ret.append(buffer);
  if (table_options_.persistent_cache) {
    snprintf(buffer, kBufferSize, "  persistent_cache_options:, ");
    ret.append(buffer);
    ret.append(table_options_.persistent_cache->GetOptionJson());
  }
*/
  snprintf(buffer, kBufferSize, "  block_size: %" ROCKSDB_PRIszt ", ",
           table_options_.block_size);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_size_deviation: %d, ",
           table_options_.block_size_deviation);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_restart_interval: %d, ",
           table_options_.block_restart_interval);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  index_block_restart_interval: %d, ",
           table_options_.index_block_restart_interval);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  metadata_block_size: %" PRIu64 ", ",
           table_options_.metadata_block_size);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  partition_filters: %d, ",
           table_options_.partition_filters);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  use_delta_encoding: %d, ",
           table_options_.use_delta_encoding);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  filter_policy: %s, ",
           table_options_.filter_policy == nullptr
           ? "nullptr"
           : table_options_.filter_policy->Name());
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  whole_key_filtering: %d, ",
           table_options_.whole_key_filtering);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  verify_compression: %d, ",
           table_options_.verify_compression);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  read_amp_bytes_per_bit: %d, ",
           table_options_.read_amp_bytes_per_bit);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  format_version: %d, ",
           table_options_.format_version);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  enable_index_compression: %d, ",
           table_options_.enable_index_compression);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_align: %d",
           table_options_.block_align);
  ret.append(buffer);
  ret.append("}");
  return ret;
}
#endif

static std::shared_ptr<TableFactory>
NewBlockBasedTableFactoryFromJson(const json& j, const JsonOptionsRepo& repo, Status* s) {
  BlockBasedTableOptions_Json _table_options;
  *s = _table_options.UpdateFromJson(j, repo);
  if (s->ok())
    return std::make_shared<BlockBasedTableFactory>(_table_options);
  else
    return nullptr;
}

ROCKSDB_FACTORY_REG("BlockBasedTable", NewBlockBasedTableFactoryFromJson);

////////////////////////////////////////////////////////////////////////////
struct PlainTableOptions_Json : PlainTableOptions {
  Status UpdateFromJson(const json& js) {
    try {
      ROCKSDB_JSON_OPT_PROP(js, user_key_len);
      ROCKSDB_JSON_OPT_PROP(js, bloom_bits_per_key);
      ROCKSDB_JSON_OPT_PROP(js, hash_table_ratio);
      ROCKSDB_JSON_OPT_PROP(js, index_sparseness);
      ROCKSDB_JSON_OPT_ENUM(js, encoding_type);
      ROCKSDB_JSON_OPT_PROP(js, full_scan_mode);
      ROCKSDB_JSON_OPT_PROP(js, store_index_in_file);
      return Status::OK();
    } catch (const std::exception& ex) {
      return Status::InvalidArgument(ROCKSDB_FUNC, ex.what());
    }
  }
};

static std::shared_ptr<TableFactory>
NewPlainTableFactoryFromJson(const json& j, const JsonOptionsRepo&, Status* s) {
  PlainTableOptions_Json options;
  *s = options.UpdateFromJson(j);
  if (s->ok())
    return std::make_shared<PlainTableFactory>(options);
  else
    return nullptr;
}

ROCKSDB_FACTORY_REG("PlainTable", NewPlainTableFactoryFromJson);

////////////////////////////////////////////////////////////////////////////
struct CuckooTableOptions_Json : CuckooTableOptions {
  Status UpdateFromJson(const json& js) {
    try {
      ROCKSDB_JSON_OPT_PROP(js, hash_table_ratio);
      ROCKSDB_JSON_OPT_PROP(js, max_search_depth);
      ROCKSDB_JSON_OPT_PROP(js, hash_table_ratio);
      ROCKSDB_JSON_OPT_PROP(js, cuckoo_block_size);
      ROCKSDB_JSON_OPT_PROP(js, identity_as_first_hash);
      ROCKSDB_JSON_OPT_PROP(js, use_module_hash);
      return Status::OK();
    } catch (const std::exception& ex) {
      return Status::InvalidArgument(ROCKSDB_FUNC, ex.what());
    }
  }
};

static std::shared_ptr<TableFactory>
NewCuckooTableFactoryJson(const json& j, const JsonOptionsRepo&, Status* s) {
  CuckooTableOptions_Json options;
  *s = options.UpdateFromJson(j);
  if (s->ok())
    return std::shared_ptr<TableFactory>(NewCuckooTableFactory(options));
  else
    return nullptr;
}

ROCKSDB_FACTORY_REG("CuckooTable", NewCuckooTableFactoryJson);

////////////////////////////////////////////////////////////////////////////

extern const uint64_t kPlainTableMagicNumber;
extern const uint64_t kLegacyPlainTableMagicNumber;
extern const uint64_t kBlockBasedTableMagicNumber;
extern const uint64_t kLegacyBlockBasedTableMagicNumber;
extern const uint64_t kCuckooTableMagicNumber;

// plugin TableFactory can using this function to register
// its TableMagicNumber
std::unordered_map<uint64_t, std::string>&
GetDispatherTableMagicNumberMap() {
  static std::unordered_map<uint64_t, std::string> map {
      {kPlainTableMagicNumber, "PlainTable"},
      {kLegacyPlainTableMagicNumber, "PlainTable"},
      {kBlockBasedTableMagicNumber, "BlockBasedTable"},
      {kLegacyBlockBasedTableMagicNumber, "BlockBasedTable"},
      {kCuckooTableMagicNumber, "CuckooTable"},
  };
  return map;
}

class DispatherTableFactory : public TableFactory {
 public:
  ~DispatherTableFactory() {}

  DispatherTableFactory(const json& js, const JsonOptionsRepo& repo) {
    m_json_obj = js; // backup
    m_json_str = js.dump();
    m_repo.reset(new JsonOptionsRepo(repo)); // backup
  }

  const char* Name() const override { return "DispatherTableFactory"; }

  Status NewTableReader(
      const ReadOptions& ro,
      const TableReaderOptions& table_reader_options,
      std::unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
      std::unique_ptr<TableReader>* table,
      bool prefetch_index_and_filter_in_cache) const override {
    assert(m_repo.get() == nullptr);
    if (m_repo) {
      return Status::InvalidArgument(
          ROCKSDB_FUNC, "SanitizeOptions() was not called");
    }
    Footer footer;
    auto s = ReadFooterFromFile(IOOptions(),
                                file.get(), nullptr /* prefetch_buffer */,
                                file_size, &footer);
    if (!s.ok()) {
      return s;
    }
    auto& map = GetDispatherTableMagicNumberMap();
    auto iter = map.find(footer.table_magic_number());
    if (map.end() != iter) {
      const std::string& factory_name = iter->second;
      auto fp_iter = m_all->find(factory_name);
      if (m_all->end() != fp_iter) {
        const std::shared_ptr<TableFactory>& factory = fp_iter->second;
        return factory->NewTableReader(ro, table_reader_options,
                                       std::move(file), file_size, table,
                                       prefetch_index_and_filter_in_cache);
      } else {
        std::string msg;
        msg += "MagicNumber = ";
        msg += Slice((char*)&iter->first, 8).ToString(true);
        msg += " -> Factory inst_id = " + factory_name;
        msg += " is defined but this inst_id is not defined,"
            " this should was check in SanitizeOptions(),"
            " but is seems SanitizeOptions() was not called,"
            " or new MagicNumber was registered after SanitizeOptions()"
            " but the inst_id was not registered";
        return Status::Corruption(ROCKSDB_FUNC, msg);
      }
    }
    return Status::NotSupported("Unidentified table format");
  }

  TableBuilder* NewTableBuilder(
      const TableBuilderOptions& table_builder_options,
      uint32_t column_family_id, WritableFileWriter* file)
  const override {
    assert(m_repo.get() == nullptr);
    if (m_repo) {
      fprintf(stderr, "FATAL: %s:%d: %s: %s\n",
              __FILE__, __LINE__, ROCKSDB_FUNC,
              "SanitizeOptions() was not called");
      abort();
    }
    int level = table_builder_options.level;
    if (size_t(level) < m_level_writers.size()) {
      return m_level_writers[level]->NewTableBuilder(
          table_builder_options, column_family_id, file);
    }
    else {
      return m_default_writer->NewTableBuilder(
          table_builder_options, column_family_id, file);
    }
  }

  // Sanitizes the specified DB Options.
  Status SanitizeOptions(const DBOptions&, const ColumnFamilyOptions&)
  const override {
    Status s;
    if (m_repo) {
      // m_repo is non-null indicate BackPatch was not called
      s = BackPatch();
    }
    return s;
  }
  Status BackPatch() const try {
    m_repo->GetMap(&m_all);
    if (!m_json_obj.is_object()) {
      return Status::InvalidArgument(ROCKSDB_FUNC,
          "DispatherTableFactory options must be object");
    }
    auto iter = m_json_obj.find("default");
    Status s;
    if (m_json_obj.end() != iter) {
      auto& subjs = iter.value();
      m_default_writer = PluginFactory<std::shared_ptr<TableFactory>>::
        ObtainPlugin("default", ROCKSDB_FUNC, subjs, *m_repo, &s);
      if (!m_default_writer) {
        return Status::InvalidArgument(ROCKSDB_FUNC,
            "fail get defined default writer = " + subjs.dump());
      }
    } else {
      auto iter2 = m_all->find("default");
      if (m_all->end() != iter2) {
        m_default_writer = iter2->second;
      } else {
        return Status::InvalidArgument(ROCKSDB_FUNC,
            "fail get global default Factory");
      }
    }
    iter = m_json_obj.find("level_writers");
    if (m_json_obj.end() != iter) {
      if (!iter.value().is_array()) {
        throw std::invalid_argument(
            "DispatherTableFactory level_writers must be array");
      }
      for (auto& item : m_json_obj.items()) {
        auto& options = item.value();
        auto p = PluginFactory<std::shared_ptr<TableFactory>>::
        ObtainPlugin("default", ROCKSDB_FUNC, options, *m_repo, &s);
        if (!p) {
          assert(!s.ok());
          return s;
        }
        m_level_writers.push_back(p);
      }
    }
    for (auto& kv : GetDispatherTableMagicNumberMap()) {
      const std::string& factory_name = kv.second;
      auto fp_iter = m_all->find(factory_name);
      if (m_all->end() == fp_iter) {
        std::string msg;
        msg += "MagicNumber = ";
        msg += Slice((char*)&kv.first, 8).ToString(true);
        msg += " -> Factory inst_id = " + factory_name;
        msg += " is defined but this inst_id is not defined";
        return Status::InvalidArgument(ROCKSDB_FUNC, msg);
      }
    }
    m_repo.reset();
    m_json_obj = json{}; // reset
    return s;
  }
  catch (const std::exception& ex) {
    return Status::InvalidArgument(ROCKSDB_FUNC, ex.what());
  }

  std::string GetPrintableTableOptions() const override {
    return m_json_str;
  }

  Status GetOptionString(const ConfigOptions&,
                         std::string* opt_string) const override {
      *opt_string = m_json_str;
      return Status::OK();
  }

  mutable std::vector<std::shared_ptr<TableFactory> > m_level_writers;
  mutable std::shared_ptr<TableFactory> m_default_writer;
  mutable std::shared_ptr<std::unordered_map<std::string,
                                     std::shared_ptr<TableFactory>>> m_all;
  mutable std::string m_json_str;
  mutable json m_json_obj{}; // reset to null after back patched
  mutable std::unique_ptr<JsonOptionsRepo> m_repo; // for back patch
};

static std::shared_ptr<TableFactory>
NewDispatcherTableFactoryJson(const json& js,
                              const JsonOptionsRepo& repo, Status* s)
try {
  return std::make_shared<DispatherTableFactory>(js, repo);
}
catch (const std::exception& ex) {
  *s = Status::InvalidArgument(ROCKSDB_FUNC, ex.what());
  return nullptr;
}
catch (const Status& es) {
  *s = es;
  return nullptr;
}
ROCKSDB_FACTORY_REG("Dispath", NewDispatcherTableFactoryJson);
ROCKSDB_FACTORY_REG("Dispather", NewDispatcherTableFactoryJson);
ROCKSDB_FACTORY_REG("DispatherTable", NewDispatcherTableFactoryJson);

}
