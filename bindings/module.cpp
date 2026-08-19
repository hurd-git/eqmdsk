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

#include "eqmdsk/cocos.hpp"
#include "eqmdsk/error.hpp"
#include "eqmdsk/field.hpp"
#include "eqmdsk/file.hpp"
#include "eqmdsk/gfile.hpp"
#include "eqmdsk/raw_section.hpp"
#include "eqmdsk/version.hpp"

namespace py = pybind11;

namespace {

PyObject* cocos_error_type = nullptr;

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

py::object field_to_python(eqmdsk::FieldMap& fields, const std::string& name,
                           py::handle owner) {
  auto& value = fields.at(name);
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

void assign_field(eqmdsk::FieldMap& fields, const std::string& name,
                  py::handle source) {
  auto& value = fields.at(name);
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
          item.resize(array.shape(0), array.shape(1));
          std::copy_n(array.data(), array.size(), item.data());
        } else if constexpr (std::is_same_v<T, eqmdsk::DoubleVector>) {
          auto array = py::array_t<double, py::array::c_style |
                                               py::array::forcecast>::ensure(source);
          if (!array || array.ndim() != 1) {
            throw py::value_error(name + " requires a one-dimensional float64 array");
          }
          item.resize(array.shape(0));
          std::copy_n(array.data(), array.size(), item.data());
        } else if constexpr (std::is_same_v<T, eqmdsk::IntVector>) {
          auto array = py::array_t<std::int64_t, py::array::c_style |
                                                      py::array::forcecast>::ensure(source);
          if (!array || array.ndim() != 1) {
            throw py::value_error(name + " requires a one-dimensional int64 array");
          }
          item.resize(array.shape(0));
          std::copy_n(array.data(), array.size(), item.data());
        }
      },
      value);
}

}  // namespace

PYBIND11_MODULE(_core, module) {
  module.doc() = "C++ core for eqmdsk";
  module.attr("__version__") = EQMDSK_VERSION_STRING;

  auto error = py::register_exception<eqmdsk::Error>(module, "Error");
  py::register_exception<eqmdsk::IOError>(module, "IOError", error.ptr());
  py::register_exception<eqmdsk::ParseError>(module, "ParseError", error.ptr());
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
                             py::overload_cast<>(&eqmdsk::EFITFile::raw_sections),
                             py::return_value_policy::reference_internal)
      .def("keys", &eqmdsk::EFITFile::keys)
      .def("__len__", [](const eqmdsk::EFITFile& self) {
        return self.fields().size();
      })
      .def("__contains__", [](const eqmdsk::EFITFile& self,
                              const std::string& name) {
        return self.fields().contains(name);
      })
      .def("__getitem__", [](eqmdsk::EFITFile& self, const std::string& name) {
        return field_to_python(
            self.fields(), name,
            py::cast(&self, py::return_value_policy::reference));
      })
      .def("__setitem__", [](eqmdsk::EFITFile& self, const std::string& name,
                             py::handle value) {
        assign_field(self.fields(), name, value);
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
      .def_property_readonly("extra_header", &eqmdsk::GFile::extra_header)
      .def_property_readonly("extension_tail", [](const eqmdsk::GFile& self) {
        return py::bytes(self.extension_tail());
      });
}
