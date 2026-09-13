/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/unicode_data.h"
#include "xlang3/value.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace xlang3 {
namespace {

bool decode_single(std::string_view text, uint32_t& codepoint) {
  if (text.empty()) return false;
  const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[0]));
  if (width == 0 || width != text.size()) return false;
  if (width == 1) { codepoint = static_cast<unsigned char>(text[0]); return true; }
  codepoint = static_cast<unsigned char>(text[0]) & ((1u << (7 - width)) - 1u);
  for (size_t index = 1; index < width; ++index) {
    const unsigned char byte = static_cast<unsigned char>(text[index]);
    if ((byte & 0xc0u) != 0x80u) return false;
    codepoint = (codepoint << 6) | (byte & 0x3fu);
  }
  return codepoint <= 0x10ffff;
}

bool get_character(Runtime& runtime, const Value& value, const char* function,
                   uint32_t& codepoint, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    error = std::string(function) + "() argument must be a unicode character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!decode_single(string_object_view(*string), codepoint)) {
    error = std::string(function) + "() argument must be a unicode character, not str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool check_count(Runtime& runtime, uint32_t argc, uint32_t low, uint32_t high,
                 const char* function, std::string& error) {
  if (argc >= low && argc <= high) return true;
  error = std::string("unicodedata.") + function + "() expected " +
      (low == high ? std::to_string(low) : "one or two") + " argument(s)";
  runtime.raise_class_error("TypeError", error);
  return false;
}

template <bool Legacy>
bool lookup(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
            std::string& error, void*) {
  if (!check_count(runtime, argc, 1, 1, "lookup", error)) return false;
  auto* name = value_as_string(args[0]);
  if (name == nullptr) {
    error = "unicodedata.lookup() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string value;
  const std::string requested = string_object_to_string(*name);
  if (!(Legacy ? unicode_legacy_data_lookup(requested, value) : unicode_data_lookup(requested, value))) {
    error = "undefined character name '" + requested + "'";
    runtime.raise_class_error("KeyError", error);
    return false;
  }
  out = Value::string(std::move(value));
  return true;
}

template <bool Legacy>
bool name(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
          std::string& error, void*) {
  if (!check_count(runtime, argc, 1, 2, "name", error)) return false;
  uint32_t codepoint = 0;
  if (!get_character(runtime, args[0], "name", codepoint, error)) return false;
  std::string result = Legacy ? unicode_legacy_data_name(codepoint) : unicode_data_name(codepoint);
  if (!result.empty()) { out = Value::string(std::move(result)); return true; }
  if (argc == 2) { out = args[1]; return true; }
  error = "no such name";
  runtime.raise_class_error("ValueError", error);
  return false;
}

enum class StringProperty { Category, Bidirectional, EastAsianWidth };
template <bool Legacy>
bool string_property(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                     std::string& error, const char* function, StringProperty property) {
  if (!check_count(runtime, argc, 1, 1, function, error)) return false;
  uint32_t codepoint = 0;
  if (!get_character(runtime, args[0], function, codepoint, error)) return false;
  const auto record = Legacy ? unicode_legacy_data_record(codepoint) : unicode_data_record(codepoint);
  const std::string_view result = property == StringProperty::Category ? record.category :
      property == StringProperty::Bidirectional ? record.bidirectional : record.east_asian_width;
  out = Value::string(std::string(result));
  return true;
}

template <bool L> bool category(Runtime& r,const Value* a,uint32_t n,Value& o,std::string& e,void*) { return string_property<L>(r,a,n,o,e,"category",StringProperty::Category); }
template <bool L> bool bidirectional(Runtime& r,const Value* a,uint32_t n,Value& o,std::string& e,void*) { return string_property<L>(r,a,n,o,e,"bidirectional",StringProperty::Bidirectional); }
template <bool L> bool east_asian_width(Runtime& r,const Value* a,uint32_t n,Value& o,std::string& e,void*) { return string_property<L>(r,a,n,o,e,"east_asian_width",StringProperty::EastAsianWidth); }

template <bool Legacy> bool combining(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  if (!check_count(runtime,argc,1,1,"combining",error)) return false;
  uint32_t cp=0; if(!get_character(runtime,args[0],"combining",cp,error)) return false;
  out=Value::int64((Legacy ? unicode_legacy_data_record(cp) : unicode_data_record(cp)).combining); return true;
}
template <bool Legacy> bool mirrored(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  if (!check_count(runtime,argc,1,1,"mirrored",error)) return false;
  uint32_t cp=0; if(!get_character(runtime,args[0],"mirrored",cp,error)) return false;
  out=Value::int64((Legacy ? unicode_legacy_data_record(cp) : unicode_data_record(cp)).mirrored ? 1 : 0); return true;
}

template <bool Decimal, bool Legacy>
bool integer_numeric(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  const char* function = Decimal ? "decimal" : "digit";
  if (!check_count(runtime,argc,1,2,function,error)) return false;
  uint32_t cp=0; if(!get_character(runtime,args[0],function,cp,error)) return false;
  const auto record=Legacy ? unicode_legacy_data_record(cp) : unicode_data_record(cp);
  const int value=Decimal ? record.decimal : record.digit;
  if(value>=0) { out=Value::int64(value); return true; }
  if(argc==2) { out=args[1]; return true; }
  error="not a " + std::string(function) + " character";
  runtime.raise_class_error("ValueError",error); return false;
}

template <bool Legacy> bool numeric(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  if (!check_count(runtime,argc,1,2,"numeric",error)) return false;
  uint32_t cp=0; if(!get_character(runtime,args[0],"numeric",cp,error)) return false;
  double value=0; if(Legacy ? unicode_legacy_data_numeric(cp,value) : unicode_data_numeric(cp,value)) { out=Value::number(value); return true; }
  if(argc==2) { out=args[1]; return true; }
  error="not a numeric character"; runtime.raise_class_error("ValueError",error); return false;
}

template <bool Legacy> bool decomposition(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  if (!check_count(runtime,argc,1,1,"decomposition",error)) return false;
  uint32_t cp=0; if(!get_character(runtime,args[0],"decomposition",cp,error)) return false;
  out=Value::string(std::string(Legacy ? unicode_legacy_data_decomposition(cp) : unicode_data_decomposition(cp))); return true;
}

template <bool Legacy> bool normalize(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  if (!check_count(runtime,argc,2,2,"normalize",error)) return false;
  auto* form_value=value_as_string(args[0]); auto* text_value=value_as_string(args[1]);
  if(!form_value || !text_value) { error="normalize() argument 1 and 2 must be str"; runtime.raise_class_error("TypeError",error); return false; }
  const std::string form=string_object_to_string(*form_value);
  if(form!="NFC"&&form!="NFD"&&form!="NFKC"&&form!="NFKD") { error="invalid normalization form"; runtime.raise_class_error("ValueError",error); return false; }
  std::string result;
  if(!(Legacy ? unicode_legacy_data_normalize(form,string_object_view(*text_value),result) : unicode_data_normalize(form,string_object_view(*text_value),result))) { error="normalization failed"; runtime.raise_class_error("ValueError",error); return false; }
  out=Value::string(std::move(result)); return true;
}

template <bool Legacy> bool is_normalized(Runtime& runtime,const Value* args,uint32_t argc,Value& out,std::string& error,void*) {
  Value normalized; if(!normalize<Legacy>(runtime,args,argc,normalized,error,nullptr)) return false;
  out=Value::boolean(string_object_view(*value_as_string(args[1]))==string_object_view(*value_as_string(normalized)));
  return true;
}

}  // namespace

void register_unicodedata_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime,"unicodedata");
  builder.function("lookup",lookup<false>).function("name",name<false>).function("category",category<false>)
      .function("bidirectional",bidirectional<false>).function("combining",combining<false>)
      .function("east_asian_width",east_asian_width<false>).function("mirrored",mirrored<false>)
      .function("decimal",integer_numeric<true,false>).function("digit",integer_numeric<false,false>)
      .function("numeric",numeric<false>).function("decomposition",decomposition<false>)
      .function("normalize",normalize<false>).function("is_normalized",is_normalized<false>)
      .value("unidata_version",Value::string(unicode_data_version()));
  Value module=builder.finish();
  NativeModuleBuilder legacy_builder(runtime,std::string("unicodedata") + ".ucd_3_2_0");
  legacy_builder.function("lookup",lookup<true>).function("name",name<true>).function("category",category<true>)
      .function("bidirectional",bidirectional<true>).function("combining",combining<true>)
      .function("east_asian_width",east_asian_width<true>).function("mirrored",mirrored<true>)
      .function("decimal",integer_numeric<true,true>).function("digit",integer_numeric<false,true>)
      .function("numeric",numeric<true>).function("decomposition",decomposition<true>)
      .function("normalize",normalize<true>).function("is_normalized",is_normalized<true>)
      .value("unidata_version",Value::string("3.2.0"));
  Value legacy_module=legacy_builder.finish();
  std::string ignored;
  std::vector<std::pair<std::string,Value>> attrs;
  for(const char* member:{"lookup","name","category","bidirectional","combining","east_asian_width","mirrored","decimal","digit","numeric","decomposition","normalize","is_normalized"}) {
    Value value; if(module_get_attr(legacy_module,member,value,ignored)) attrs.push_back({member,std::move(value)});
  }
  attrs.push_back({"unidata_version",Value::string("3.2.0")});
  Value legacy=Value::instance(Value::class_object("UCD",std::move(attrs)));
  module_ensure_attr_slots(module,{"ucd_3_2_0"},ignored);
  module_set_attr(module,"ucd_3_2_0",legacy,ignored);
  runtime.register_module("unicodedata",std::move(module));
}

}  // namespace xlang3
