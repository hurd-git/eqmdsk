#include <pybind11/complex.h>
#include <pybind11/eigen.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "eqmdsk/afile.hpp"
#include "eqmdsk/cocos.hpp"
#include "eqmdsk/error.hpp"
#include "eqmdsk/field.hpp"
#include "eqmdsk/file.hpp"
#include "eqmdsk/gfile.hpp"
#include "eqmdsk/kfile.hpp"
#include "eqmdsk/raw_section.hpp"
#include "eqmdsk/sfile.hpp"
#include "eqmdsk/version.hpp"

namespace py = pybind11;

namespace {

PyObject* cocos_error_type = nullptr;
PyObject* parse_error_type = nullptr;

void translate_cocos_error(std::exception_ptr exception) {
  try {
    if (exception) {
      std::rethrow_exception(exception);
    }
  } catch (const eqmdsk::CocosError& error) {
    py::gil_scoped_acquire acquire;
    py::object type = py::reinterpret_borrow<py::object>(cocos_error_type);
    py::object instance = type(py::str(error.what()));
    instance.attr("result") = py::cast(error.result());
    PyErr_SetObject(cocos_error_type, instance.ptr());
  }
}

void translate_parse_error(std::exception_ptr exception) {
  try {
    if (exception) {
      std::rethrow_exception(exception);
    }
  } catch (const eqmdsk::ParseError& error) {
    py::gil_scoped_acquire acquire;
    py::object type = py::reinterpret_borrow<py::object>(parse_error_type);
    py::object instance = type(py::str(error.what()));
    instance.attr("filename") = error.filename();
    instance.attr("line") = error.line();
    instance.attr("column") = error.column();
    PyErr_SetObject(parse_error_type, instance.ptr());
  }
}

template <typename EigenType>
py::array eigen_view(EigenType& value, py::handle owner) {
  using Scalar = typename EigenType::Scalar;
  if constexpr (EigenType::IsVectorAtCompileTime) {
    return py::array(py::dtype::of<Scalar>(),
                     {static_cast<py::ssize_t>(value.size())},
                     {static_cast<py::ssize_t>(sizeof(Scalar))}, value.data(),
                     owner);
  } else {
    return py::array(
        py::dtype::of<Scalar>(),
        {static_cast<py::ssize_t>(value.rows()),
         static_cast<py::ssize_t>(value.cols())},
        {static_cast<py::ssize_t>(sizeof(Scalar) * value.cols()),
         static_cast<py::ssize_t>(sizeof(Scalar))},
        value.data(), owner);
  }
}

py::object field_value_to_python(eqmdsk::FieldValue& value,
                                 py::handle owner) {
  return std::visit(
      [&](auto& item) -> py::object {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, eqmdsk::IntVector> ||
                      std::is_same_v<T, eqmdsk::DoubleVector> ||
                      std::is_same_v<T, eqmdsk::DoubleMatrix>) {
          return eigen_view(item, owner);
        } else {
          return py::cast(item);
        }
      },
      value);
}

py::object field_to_python(eqmdsk::FieldMap& fields, const std::string& name,
                           py::handle owner) {
  return field_value_to_python(fields.at(name), owner);
}

void assign_field_value(eqmdsk::FieldValue& value, const std::string& name,
                        py::handle source) {
  std::visit(
      [&](auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, bool> ||
                      std::is_same_v<T, std::int64_t> ||
                      std::is_same_v<T, double> ||
                      std::is_same_v<T, std::string> ||
                      std::is_same_v<T, eqmdsk::StringVector>) {
          item = py::cast<T>(source);
        } else if constexpr (std::is_same_v<T, eqmdsk::DoubleMatrix>) {
          auto array = py::array_t<double, py::array::c_style |
                                               py::array::forcecast>::ensure(source);
          if (!array || array.ndim() != 2) {
            throw py::value_error(name + " requires a two-dimensional float64 array");
          }
          if (array.shape(0) != item.rows() || array.shape(1) != item.cols()) {
            throw py::value_error(
                name + " assignment must preserve the existing array shape");
          }
          std::copy_n(array.data(), array.size(), item.data());
        } else if constexpr (std::is_same_v<T, eqmdsk::DoubleVector>) {
          auto array = py::array_t<double, py::array::c_style |
                                               py::array::forcecast>::ensure(source);
          if (!array || array.ndim() != 1) {
            throw py::value_error(name + " requires a one-dimensional float64 array");
          }
          if (array.shape(0) != item.size()) {
            throw py::value_error(
                name + " assignment must preserve the existing array length");
          }
          std::copy_n(array.data(), array.size(), item.data());
        } else if constexpr (std::is_same_v<T, eqmdsk::IntVector>) {
          auto array = py::array_t<std::int64_t, py::array::c_style |
                                                      py::array::forcecast>::ensure(source);
          if (!array || array.ndim() != 1) {
            throw py::value_error(name + " requires a one-dimensional int64 array");
          }
          if (array.shape(0) != item.size()) {
            throw py::value_error(
                name + " assignment must preserve the existing array length");
          }
          std::copy_n(array.data(), array.size(), item.data());
        }
      },
      value);
}

void assign_field(eqmdsk::FieldMap& fields, const std::string& name,
                  py::handle source) {
  assign_field_value(fields.at(name), name, source);
}

py::object namelist_value_to_python(const eqmdsk::NamelistValue& value) {
  if (value.kind() == eqmdsk::NamelistValueKind::null) {
    return py::none();
  }
  return std::visit(
      [](const auto& item) -> py::object { return py::cast(item); },
      value.storage());
}

}  // namespace

PYBIND11_MODULE(_core, module) {
  module.doc() = "C++ core for eqmdsk";
  module.attr("__version__") = EQMDSK_VERSION_STRING;

  auto error = py::register_exception<eqmdsk::Error>(module, "Error");
  py::register_exception<eqmdsk::IOError>(module, "IOError", error.ptr());
  auto parse_error = py::register_exception<eqmdsk::ParseError>(
      module, "ParseError", error.ptr());
  parse_error_type = parse_error.ptr();
  py::register_local_exception_translator(&translate_parse_error);
  py::register_exception<eqmdsk::ValidationError>(module, "ValidationError",
                                                  error.ptr());
  py::register_exception<eqmdsk::FieldError>(module, "FieldError", error.ptr());

  py::class_<eqmdsk::CocosResult>(module, "CocosResult")
      .def(py::init<>())
      .def_property_readonly("candidates", &eqmdsk::CocosResult::candidates)
      .def_property_readonly("diagnostic", &eqmdsk::CocosResult::diagnostic)
      .def_property_readonly("selected", &eqmdsk::CocosResult::selected)
      .def("is_unique", &eqmdsk::CocosResult::is_unique)
      .def("is_ambiguous", &eqmdsk::CocosResult::is_ambiguous)
      .def("has_match", &eqmdsk::CocosResult::has_match)
      .def("__repr__", [](const eqmdsk::CocosResult& result) {
        return "CocosResult(candidates=" +
               py::repr(py::cast(result.candidates())).cast<std::string>() +
               ", diagnostic=" +
               py::repr(py::cast(result.diagnostic())).cast<std::string>() +
               ")";
      });

  py::object cocos_error = py::reinterpret_steal<py::object>(PyErr_NewException(
      "eqmdsk._core.CocosError", error.ptr(), nullptr));
  module.attr("CocosError") = cocos_error;
  cocos_error_type = cocos_error.ptr();
  py::register_local_exception_translator(&translate_cocos_error);

  py::class_<eqmdsk::RawSection>(module, "RawSection")
      .def_property_readonly("name", [](const eqmdsk::RawSection& self) {
        return self.name;
      })
      .def_property_readonly("data", [](const eqmdsk::RawSection& self) {
        return py::bytes(self.data);
      })
      .def_readonly("source_offset", &eqmdsk::RawSection::source_offset)
      .def_readonly("modified", &eqmdsk::RawSection::modified);

  py::class_<eqmdsk::FieldMap>(module, "FieldMap")
      .def(py::init<>())
      .def("keys", &eqmdsk::FieldMap::keys)
      .def("contains", &eqmdsk::FieldMap::contains)
      .def("type_name", [](const eqmdsk::FieldMap& self,
                            const std::string& name) {
        return eqmdsk::field_type_name(self.at(name));
      })
      .def("_insert_float", [](eqmdsk::FieldMap& self, const std::string& name,
                                double value) {
        self.insert(name, value);
      })
      .def("_insert_matrix",
           [](eqmdsk::FieldMap& self, const std::string& name,
              const Eigen::Ref<const eqmdsk::DoubleMatrix>& value) {
             self.insert(name, eqmdsk::DoubleMatrix(value));
           })
      .def("__len__", &eqmdsk::FieldMap::size)
      .def("__contains__", &eqmdsk::FieldMap::contains)
      .def("__getitem__",
           [](eqmdsk::FieldMap& self, const std::string& name) {
             return field_to_python(self, name,
                                    py::cast(&self, py::return_value_policy::reference));
           })
      .def("__setitem__", &assign_field)
      .def("__iter__",
           [](const eqmdsk::FieldMap& self) {
             return py::iter(py::cast(self.keys()));
           },
           py::keep_alive<0, 1>());

  py::class_<eqmdsk::EFITFile>(module, "EFITFile")
      .def_property_readonly("filename", [](const eqmdsk::EFITFile& self) {
        return self.filename();
      })
      .def_property_readonly("fields",
                             py::overload_cast<>(&eqmdsk::EFITFile::fields),
                             py::return_value_policy::reference_internal)
      .def_property_readonly("raw_sections",
                             py::overload_cast<>(
                                 &eqmdsk::EFITFile::raw_sections, py::const_),
                             py::return_value_policy::reference_internal)
      .def("keys", &eqmdsk::EFITFile::keys)
      .def("__len__", [](const eqmdsk::EFITFile& self) {
        return self.fields().size();
      })
      .def("__contains__", [](const eqmdsk::EFITFile& self,
                              const std::string& name) {
        return self.contains(name);
      })
      .def("__getitem__", [](eqmdsk::EFITFile& self, const std::string& name) {
        return field_value_to_python(
            self.at(name),
            py::cast(&self, py::return_value_policy::reference));
      })
      .def("__setitem__", [](eqmdsk::EFITFile& self, const std::string& name,
                             py::handle value) {
        assign_field_value(self.at(name), name, value);
      });

  py::class_<eqmdsk::GFile, eqmdsk::EFITFile>(module, "GFile")
      .def(py::init<const std::filesystem::path&>(), py::arg("filename"))
      .def("write",
           [](const eqmdsk::GFile& self,
              const std::optional<std::filesystem::path>& path) {
             if (path) {
               self.write(*path);
             } else {
               self.write();
             }
           },
           py::arg("path") = py::none())
      .def_property_readonly("cocos", &eqmdsk::GFile::cocos,
                             py::return_value_policy::reference_internal)
      .def("select_cocos", &eqmdsk::GFile::select_cocos, py::arg("source"))
      .def("to_cocos",
           [](eqmdsk::GFile& self, int target, bool inplace) -> py::object {
             if (inplace) {
               self.to_cocos(target);
               return py::cast(&self, py::return_value_policy::reference);
             }
             return py::cast(self.converted_to_cocos(target));
           },
           py::arg("target"), py::arg("inplace") = true)
      .def_property_readonly("extra_header", [](const eqmdsk::GFile& self) {
        return py::bytes(self.extra_header());
      })
      .def_property_readonly("extension_tail", [](const eqmdsk::GFile& self) {
        return py::bytes(self.extension_tail());
      });

  py::class_<eqmdsk::AFile, eqmdsk::EFITFile>(module, "AFile")
      .def(py::init<const std::filesystem::path&>(), py::arg("filename"))
      .def("write",
           [](const eqmdsk::AFile& self,
              const std::optional<std::filesystem::path>& path) {
             if (path) {
               self.write(*path);
             } else {
               self.write();
             }
           },
           py::arg("path") = py::none())
      .def_property_readonly("header", [](const eqmdsk::AFile& self) {
        return py::bytes(self.header());
      })
      .def_property_readonly("footer", [](const eqmdsk::AFile& self) {
        return py::bytes(self.footer());
      })
      .def_property_readonly("optional_record_count",
                             &eqmdsk::AFile::optional_record_count);

  py::enum_<eqmdsk::NamelistValueKind>(module, "NamelistValueKind")
      .value("null", eqmdsk::NamelistValueKind::null)
      .value("integer", eqmdsk::NamelistValueKind::integer)
      .value("real", eqmdsk::NamelistValueKind::real)
      .value("logical", eqmdsk::NamelistValueKind::logical)
      .value("string", eqmdsk::NamelistValueKind::string)
      .value("complex", eqmdsk::NamelistValueKind::complex)
      .value("raw", eqmdsk::NamelistValueKind::raw);

  py::class_<eqmdsk::NamelistValue>(module, "NamelistValue")
      .def_static("null", &eqmdsk::NamelistValue::null,
                  py::arg("repeat") = 1)
      .def_static("integer", &eqmdsk::NamelistValue::integer,
                  py::arg("value"), py::arg("repeat") = 1)
      .def_static("real", &eqmdsk::NamelistValue::real,
                  py::arg("value"), py::arg("repeat") = 1)
      .def_static("logical", &eqmdsk::NamelistValue::logical,
                  py::arg("value"), py::arg("repeat") = 1)
      .def_static("string", &eqmdsk::NamelistValue::string,
                  py::arg("value"), py::arg("repeat") = 1)
      .def_static("complex", &eqmdsk::NamelistValue::complex,
                  py::arg("value"), py::arg("repeat") = 1)
      .def_static("raw", &eqmdsk::NamelistValue::raw,
                  py::arg("value"), py::arg("repeat") = 1)
      .def_property_readonly("kind", &eqmdsk::NamelistValue::kind)
      .def_property_readonly("repeat", &eqmdsk::NamelistValue::repeat)
      .def_property_readonly("value", &namelist_value_to_python)
      .def_property_readonly("original_text",
                             &eqmdsk::NamelistValue::original_text)
      .def("as_integer", &eqmdsk::NamelistValue::as_integer)
      .def("as_real", &eqmdsk::NamelistValue::as_real)
      .def("as_logical", &eqmdsk::NamelistValue::as_logical)
      .def("as_string", &eqmdsk::NamelistValue::as_string)
      .def("as_complex", &eqmdsk::NamelistValue::as_complex)
      .def("as_raw", &eqmdsk::NamelistValue::as_raw);

  py::class_<eqmdsk::NamelistEntry>(module, "NamelistEntry")
      .def_property_readonly("name", &eqmdsk::NamelistEntry::name)
      .def_property_readonly("original_name",
                             &eqmdsk::NamelistEntry::original_name)
      .def_property_readonly("designator", &eqmdsk::NamelistEntry::designator)
      .def_property_readonly("subscript", &eqmdsk::NamelistEntry::subscript)
      .def_property_readonly("values", &eqmdsk::NamelistEntry::values)
      .def_property_readonly("raw_text", [](const eqmdsk::NamelistEntry& self) {
        return py::bytes(self.raw_text());
      })
      .def_property_readonly("source_order",
                             &eqmdsk::NamelistEntry::source_order)
      .def_property_readonly("source_offset",
                             &eqmdsk::NamelistEntry::source_offset)
      .def_property_readonly("parsed", &eqmdsk::NamelistEntry::parsed)
      .def_property_readonly("modified", &eqmdsk::NamelistEntry::modified);

  py::class_<eqmdsk::NamelistSection>(module, "NamelistSection")
      .def_property_readonly("name", &eqmdsk::NamelistSection::name)
      .def_property_readonly("original_name",
                             &eqmdsk::NamelistSection::original_name)
      .def_property_readonly("opener", [](const eqmdsk::NamelistSection& self) {
        return std::string(1, self.opener());
      })
      .def_property_readonly("terminator",
                             &eqmdsk::NamelistSection::terminator)
      .def_property_readonly("entries", &eqmdsk::NamelistSection::entries)
      .def_property_readonly("raw_text", [](const eqmdsk::NamelistSection& self) {
        return py::bytes(self.raw_text());
      })
      .def_property_readonly("source_order",
                             &eqmdsk::NamelistSection::source_order)
      .def_property_readonly("source_offset",
                             &eqmdsk::NamelistSection::source_offset)
      .def("count", &eqmdsk::NamelistSection::count, py::arg("name"))
      .def("entry", &eqmdsk::NamelistSection::entry, py::arg("name"),
           py::arg("occurrence") = 0,
           py::return_value_policy::reference_internal);

  py::class_<eqmdsk::KFile, eqmdsk::EFITFile>(module, "KFile")
      .def(py::init<const std::filesystem::path&>(), py::arg("filename"))
      .def("write",
           [](const eqmdsk::KFile& self,
              const std::optional<std::filesystem::path>& path) {
             if (path) {
               self.write(*path);
             } else {
               self.write();
             }
           },
           py::arg("path") = py::none())
      .def_property_readonly("sections", &eqmdsk::KFile::sections)
      .def("section_count", &eqmdsk::KFile::section_count, py::arg("name"))
      .def("section", &eqmdsk::KFile::section, py::arg("name"),
           py::arg("occurrence") = 0,
           py::return_value_policy::reference_internal)
      .def("entry", &eqmdsk::KFile::entry, py::arg("section_name"),
           py::arg("name"), py::arg("occurrence") = 0,
           py::arg("section_occurrence") = 0,
           py::return_value_policy::reference_internal)
      .def("set",
           [](eqmdsk::KFile& self, const std::string& section_name,
              const std::string& name,
              std::vector<eqmdsk::NamelistValue> values,
              std::size_t occurrence, std::size_t section_occurrence) {
             if (self.contains(name)) {
               const auto& current = self.at(name);
               if (std::holds_alternative<eqmdsk::IntVector>(current) ||
                   std::holds_alternative<eqmdsk::DoubleVector>(current) ||
                   std::holds_alternative<eqmdsk::DoubleMatrix>(current)) {
                 throw py::value_error(
                     name +
                     " is an exposed array; modify its NumPy view in place");
               }
             }
             self.set(section_name, name, std::move(values), occurrence,
                      section_occurrence);
           },
           py::arg("section_name"), py::arg("name"), py::arg("values"),
           py::arg("occurrence") = 0, py::arg("section_occurrence") = 0)
      .def("keys", &eqmdsk::KFile::keys)
      .def("__contains__", &eqmdsk::KFile::contains)
      .def("__getitem__", [](eqmdsk::KFile& self, const std::string& name) {
        return field_value_to_python(
            self.at(name),
            py::cast(&self, py::return_value_policy::reference));
      })
      .def("__setitem__", [](eqmdsk::KFile& self, const std::string& name,
                              py::handle value) {
        assign_field_value(self.at(name), name, value);
      });

  py::class_<eqmdsk::SFile, eqmdsk::EFITFile>(module, "SFile")
      .def(py::init<const std::filesystem::path&>(), py::arg("filename"))
      .def("write",
           [](const eqmdsk::SFile& self,
              const std::optional<std::filesystem::path>& path) {
             if (path) {
               self.write(*path);
             } else {
               self.write();
             }
           },
           py::arg("path") = py::none());
}
