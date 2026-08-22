#include <pybind11/eigen.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>

#include "eqmdsk/afile.hpp"
#include "eqmdsk/cocos.hpp"
#include "eqmdsk/error.hpp"
#include "eqmdsk/field.hpp"
#include "eqmdsk/file.hpp"
#include "eqmdsk/gfile.hpp"
#include "eqmdsk/kfile.hpp"
#include "eqmdsk/sfile.hpp"
#include "eqmdsk/version.hpp"

namespace py = pybind11;

namespace {

std::string upper_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](char item) {
    return item >= 'a' && item <= 'z' ? static_cast<char>(item - 'a' + 'A')
                                     : item;
  });
  return value;
}

PyObject* cocos_error_type = nullptr;
PyObject* parse_error_type = nullptr;

std::string path_from_python(py::handle value) {
  return py::module_::import("os")
      .attr("fsencode")(value)
      .cast<std::string>();
}

py::str path_to_python(const std::string& value) {
  return py::module_::import("os").attr("fsdecode")(py::bytes(value));
}

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
    instance.attr("filename") = py::str(error.filename());
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

eqmdsk::FieldValue field_value_from_python(py::handle source) {
  if (py::isinstance<py::bool_>(source)) {
    return source.cast<bool>();
  }
  if (py::isinstance<py::int_>(source)) {
    return source.cast<std::int64_t>();
  }
  if (py::isinstance<py::float_>(source)) {
    return source.cast<double>();
  }
  if (py::isinstance<py::str>(source)) {
    return source.cast<std::string>();
  }
  if (auto array = py::array::ensure(source)) {
    if (array.ndim() == 1) {
      if (array.dtype().is(py::dtype::of<double>())) {
        auto values = py::array_t<double, py::array::c_style |
                                        py::array::forcecast>::ensure(array);
        eqmdsk::DoubleVector result(values.shape(0));
        std::copy_n(values.data(), values.size(), result.data());
        return result;
      }
      if (array.dtype().kind() == 'i' || array.dtype().kind() == 'u') {
        auto values = py::array_t<std::int64_t, py::array::c_style |
                                             py::array::forcecast>::ensure(array);
        eqmdsk::IntVector result(values.shape(0));
        std::copy_n(values.data(), values.size(), result.data());
        return result;
      }
      throw py::type_error(
          "one-dimensional fields require float64 or integer arrays");
    }
    if (array.ndim() == 2) {
      if (array.dtype().is(py::dtype::of<double>()) ||
          array.dtype().kind() == 'i' || array.dtype().kind() == 'u') {
        auto values = py::array_t<double, py::array::c_style |
                                        py::array::forcecast>::ensure(array);
        eqmdsk::DoubleMatrix result(values.shape(0), values.shape(1));
        std::copy_n(values.data(), values.size(), result.data());
        return result;
      }
      throw py::type_error(
          "two-dimensional fields require float64 or integer arrays");
    }
    throw py::type_error("field arrays must be one- or two-dimensional");
  }
  try {
    return source.cast<eqmdsk::StringVector>();
  } catch (const py::cast_error&) {
    throw py::type_error("field value must be a scalar or numeric array");
  }
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
            throw py::value_error(
                name + " requires a two-dimensional float64 array");
          }
          if (array.shape(0) != item.rows() ||
              array.shape(1) != item.cols()) {
            throw py::value_error(
                name + " assignment must preserve the existing array shape");
          }
          std::copy_n(array.data(), array.size(), item.data());
        } else if constexpr (std::is_same_v<T, eqmdsk::DoubleVector>) {
          auto array = py::array_t<double, py::array::c_style |
                                               py::array::forcecast>::ensure(source);
          if (!array || array.ndim() != 1) {
            throw py::value_error(
                name + " requires a one-dimensional float64 array");
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
            throw py::value_error(
                name + " requires a one-dimensional int64 array");
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

py::dict fields_as_dict(eqmdsk::FieldMap& fields, py::handle owner) {
  py::dict result;
  for (const auto& name : fields.keys()) {
    result[py::str(name)] = field_value_to_python(fields.at(name), owner);
  }
  return result;
}

py::list field_items(eqmdsk::FieldMap& fields, py::handle owner) {
  py::list result;
  for (const auto& name : fields.keys()) {
    result.append(py::make_tuple(
        name, field_value_to_python(fields.at(name), owner)));
  }
  return result;
}

py::list field_values(eqmdsk::FieldMap& fields, py::handle owner) {
  py::list result;
  for (const auto& name : fields.keys()) {
    result.append(field_value_to_python(fields.at(name), owner));
  }
  return result;
}

py::dict file_as_dict(eqmdsk::FieldFile& file, py::handle owner) {
  py::dict result;
  for (const auto& name : file.keys()) {
    result[py::str(name)] = field_value_to_python(file.at(name), owner);
  }
  return result;
}

py::list file_items(eqmdsk::FieldFile& file, py::handle owner) {
  py::list result;
  for (const auto& name : file.keys()) {
    result.append(py::make_tuple(name, field_value_to_python(file.at(name), owner)));
  }
  return result;
}

py::list file_values(eqmdsk::FieldFile& file, py::handle owner) {
  py::list result;
  for (const auto& name : file.keys()) {
    result.append(field_value_to_python(file.at(name), owner));
  }
  return result;
}

template <typename File>
void save_file(const File& self, py::handle path) {
  if (path.is_none()) {
    self.save();
  } else {
    self.save(path_from_python(path));
  }
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
  py::register_exception<eqmdsk::ValidationError>(
      module, "ValidationError", error.ptr());
  py::register_exception<eqmdsk::FieldError>(module, "FieldError",
                                              error.ptr());

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

  py::class_<eqmdsk::EFITFile>(module, "_EFITFile")
      .def_property_readonly("filename", [](const eqmdsk::EFITFile& self) {
        return path_to_python(self.filename());
      })
      .def_property_readonly("path", [](const eqmdsk::EFITFile& self) {
        return path_to_python(self.path());
      })
      .def_property_readonly("abspath", [](const eqmdsk::EFITFile& self) {
        return path_to_python(self.abspath());
      });

  py::class_<eqmdsk::FieldFile, eqmdsk::EFITFile>(module, "_FieldFile")
      .def("keys", &eqmdsk::FieldFile::keys)
      .def("_schema_fields", &eqmdsk::FieldFile::schema_fields)
      .def("_missing_fields", &eqmdsk::FieldFile::missing_fields)
      .def("_missing_optional_fields",
           &eqmdsk::FieldFile::missing_optional_fields)
      .def("_is_missing_field", &eqmdsk::FieldFile::is_missing_field)
      .def("items", [](eqmdsk::FieldFile& self) {
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        return file_items(self, owner);
      })
      .def("values", [](eqmdsk::FieldFile& self) {
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        return file_values(self, owner);
      })
      .def("get", [](eqmdsk::FieldFile& self, const std::string& name,
                      py::object fallback) {
        if (!self.contains(name)) {
          return fallback;
        }
        return field_value_to_python(
            self.at(name),
            py::cast(&self, py::return_value_policy::reference));
      }, py::arg("name"), py::arg("default") = py::none())
      .def("__len__", &eqmdsk::FieldFile::size)
      .def("__contains__", &eqmdsk::FieldFile::contains)
      .def("__getitem__", [](eqmdsk::FieldFile& self,
                              const std::string& name) {
        return field_value_to_python(
            self.at(name),
            py::cast(&self, py::return_value_policy::reference));
      })
      .def("__setitem__", [](eqmdsk::FieldFile& self,
                              const std::string& name, py::handle value) {
        if (self.contains(name)) {
          self.assign(name, field_value_from_python(value));
        } else {
          throw eqmdsk::FieldError("unknown field: " + name);
        }
      })
      .def("_assign", [](eqmdsk::FieldFile& self, const std::string& name,
                          py::handle value) {
        self.assign(name, field_value_from_python(value));
      })
      .def("_erase", &eqmdsk::FieldFile::erase)
      .def("__iter__", [](const eqmdsk::FieldFile& self) {
        return py::iter(py::cast(self.keys()));
      }, py::keep_alive<0, 1>())
      .def("__repr__", [](eqmdsk::FieldFile& self) {
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        return std::string(self.format_name()) + "(" +
               py::repr(file_as_dict(self, owner)).cast<std::string>() +
               ")";
      });

  py::class_<eqmdsk::NamelistBlock>(module, "_NamelistBlock")
      .def(py::init<>())
      .def("copy", &eqmdsk::FieldMap::copy)
      .def("keys", &eqmdsk::FieldMap::keys)
      .def("items", [](eqmdsk::FieldMap& self) {
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        return field_items(self, owner);
      })
      .def("values", [](eqmdsk::FieldMap& self) {
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        return field_values(self, owner);
      })
      .def("get", [](eqmdsk::FieldMap& self, const std::string& name,
                      py::object fallback) {
        const auto normalized = upper_ascii(name);
        if (!self.contains(normalized)) {
          return fallback;
        }
        return field_value_to_python(
            self.at(normalized),
            py::cast(&self, py::return_value_policy::reference));
      }, py::arg("name"), py::arg("default") = py::none())
      .def("__len__", &eqmdsk::FieldMap::size)
      .def("__contains__", [](const eqmdsk::FieldMap& self,
                               const std::string& name) {
        return self.contains(upper_ascii(name));
      })
      .def("__getitem__", [](eqmdsk::FieldMap& self,
                              const std::string& name) {
        const auto normalized = upper_ascii(name);
        return field_value_to_python(
            self.at(normalized),
            py::cast(&self, py::return_value_policy::reference));
      })
      .def("__setitem__", [](eqmdsk::FieldMap& self,
                              const std::string& name, py::handle value) {
        const auto normalized = upper_ascii(name);
        if (self.contains(normalized)) {
          self.assign(normalized, field_value_from_python(value));
        } else {
          throw eqmdsk::FieldError("unknown field: " + normalized);
        }
      })
      .def("_assign", [](eqmdsk::FieldMap& self, const std::string& name,
                          py::handle value) {
        self.assign(upper_ascii(name), field_value_from_python(value));
      })
      .def("_erase", [](eqmdsk::FieldMap& self, const std::string& name) {
        return self.erase(upper_ascii(name));
      })
      .def("__iter__", [](const eqmdsk::FieldMap& self) {
        return py::iter(py::cast(self.keys()));
      }, py::keep_alive<0, 1>())
      .def("__repr__", [](eqmdsk::FieldMap& self) {
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        return py::repr(fields_as_dict(self, owner)).cast<std::string>();
      });

  py::class_<eqmdsk::GFile, eqmdsk::FieldFile>(module, "GFile")
      .def_static("create", &eqmdsk::GFile::create, py::arg("nw"),
                  py::arg("nh"))
      .def(py::init([](py::handle path) {
        return eqmdsk::GFile(path_from_python(path));
      }), py::arg("path"))
      .def("copy", &eqmdsk::GFile::copy)
      .def("save", &save_file<eqmdsk::GFile>, py::arg("path") = py::none())
      .def_property_readonly("cocos", &eqmdsk::GFile::cocos,
                             py::return_value_policy::reference_internal)
      .def_property_readonly("_aux_namelist", [](eqmdsk::GFile& self) {
        return self.aux_namelist();
      }, py::return_value_policy::reference_internal)
      .def("select_cocos", &eqmdsk::GFile::select_cocos, py::arg("source"))
      .def("to_cocos", [](eqmdsk::GFile& self, int target,
                           const std::optional<int>& from_cocos,
                           bool inplace) -> py::object {
        if (inplace) {
          self.to_cocos(target, from_cocos);
          return py::cast(&self, py::return_value_policy::reference);
        }
        return py::cast(self.converted_to_cocos(target, from_cocos));
      }, py::arg("to_cocos"), py::arg("from_cocos") = py::none(),
         py::arg("inplace") = true);

  py::class_<eqmdsk::AFile, eqmdsk::FieldFile>(module, "AFile")
      .def_static("create", &eqmdsk::AFile::create)
      .def(py::init([](py::handle path) {
        return eqmdsk::AFile(path_from_python(path));
      }), py::arg("path"))
      .def("copy", &eqmdsk::AFile::copy)
      .def("save", &save_file<eqmdsk::AFile>, py::arg("path") = py::none())
      .def_property("header", &eqmdsk::AFile::header,
                    &eqmdsk::AFile::set_header)
      .def_property("footer", &eqmdsk::AFile::footer,
                    &eqmdsk::AFile::set_footer);

  py::class_<eqmdsk::Namelist>(module, "_Namelist")
      .def_static("create", &eqmdsk::Namelist::create)
      .def("copy", &eqmdsk::Namelist::copy)
      .def("keys", &eqmdsk::Namelist::keys)
      .def("_assign_block", &eqmdsk::Namelist::assign_block)
      .def("_erase_block", &eqmdsk::Namelist::erase_block)
      .def("items", [](eqmdsk::Namelist& self) {
        py::list result;
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        for (const auto& name : self.keys()) {
          result.append(py::make_tuple(
              name, py::cast(&self.at(name),
                             py::return_value_policy::reference_internal,
                             owner)));
        }
        return result;
      })
      .def("values", [](eqmdsk::Namelist& self) {
        py::list result;
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        for (const auto& name : self.keys()) {
          result.append(py::cast(&self.at(name),
                                 py::return_value_policy::reference_internal,
                                 owner));
        }
        return result;
      })
      .def("get", [](eqmdsk::Namelist& self, const std::string& name,
                      py::object fallback) -> py::object {
        if (!self.contains(name)) {
          return fallback;
        }
        return py::cast(&self.at(name),
                        py::return_value_policy::reference_internal,
                        py::cast(&self, py::return_value_policy::reference));
      }, py::arg("name"), py::arg("default") = py::none())
      .def("__len__", &eqmdsk::Namelist::size)
      .def("__contains__", &eqmdsk::Namelist::contains)
      .def("__getitem__", [](eqmdsk::Namelist& self,
                              const std::string& name) -> eqmdsk::FieldMap& {
        return self.at(name);
      }, py::return_value_policy::reference_internal)
      .def("__iter__", [](const eqmdsk::Namelist& self) {
        return py::iter(py::cast(self.keys()));
      }, py::keep_alive<0, 1>())
      .def("__repr__", [](eqmdsk::Namelist& self) {
        py::dict result;
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        for (const auto& name : self.keys()) {
          result[py::str(name)] = fields_as_dict(self.at(name), owner);
        }
        return py::repr(result).cast<std::string>();
      });

  py::class_<eqmdsk::KFile, eqmdsk::EFITFile, eqmdsk::Namelist>(module, "KFile")
      .def_static("create", &eqmdsk::KFile::create)
      .def(py::init([](py::handle path) {
        return eqmdsk::KFile(path_from_python(path));
      }), py::arg("path"))
      .def("copy", &eqmdsk::KFile::copy)
      .def("save", &save_file<eqmdsk::KFile>, py::arg("path") = py::none())
      .def("keys", &eqmdsk::KFile::keys)
      .def("_assign_block", &eqmdsk::KFile::assign_block)
      .def("_erase_block", &eqmdsk::KFile::erase_block)
      .def("items", [](eqmdsk::KFile& self) {
        py::list result;
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        for (const auto& name : self.keys()) {
          result.append(py::make_tuple(
              name, py::cast(&self.at(name),
                             py::return_value_policy::reference_internal,
                             owner)));
        }
        return result;
      })
      .def("values", [](eqmdsk::KFile& self) {
        py::list result;
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        for (const auto& name : self.keys()) {
          result.append(py::cast(&self.at(name),
                                 py::return_value_policy::reference_internal,
                                 owner));
        }
        return result;
      })
      .def("get", [](eqmdsk::KFile& self, const std::string& name,
                      py::object fallback) -> py::object {
        if (!self.contains(name)) {
          return fallback;
        }
        return py::cast(&self.at(name),
                        py::return_value_policy::reference_internal,
                        py::cast(&self, py::return_value_policy::reference));
      }, py::arg("name"), py::arg("default") = py::none())
      .def("__len__", &eqmdsk::KFile::size)
      .def("__contains__", &eqmdsk::KFile::contains)
      .def("__getitem__", [](eqmdsk::KFile& self,
                              const std::string& name) -> eqmdsk::FieldMap& {
        return self.at(name);
      }, py::return_value_policy::reference_internal)
      .def("__iter__", [](const eqmdsk::KFile& self) {
        return py::iter(py::cast(self.keys()));
      }, py::keep_alive<0, 1>())
      .def("__repr__", [](eqmdsk::KFile& self) {
        py::dict result;
        py::object owner = py::cast(&self, py::return_value_policy::reference);
        for (const auto& name : self.keys()) {
          result[py::str(name)] = fields_as_dict(self.at(name), owner);
        }
        return std::string("KFile(") +
               py::repr(result).cast<std::string>() + ")";
      });

  py::class_<eqmdsk::SFile, eqmdsk::FieldFile>(module, "SFile")
      .def_static("create", &eqmdsk::SFile::create, py::arg("count"))
      .def(py::init([](py::handle path) {
        return eqmdsk::SFile(path_from_python(path));
      }), py::arg("path"))
      .def("copy", &eqmdsk::SFile::copy)
      .def("save", &save_file<eqmdsk::SFile>, py::arg("path") = py::none());
}
