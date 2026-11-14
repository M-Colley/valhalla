#include "baldr/rapidjson_utils.h"
#include "midgard/logging.h"
#include "module.h"
#include "tyr/actor.h"

#include <boost/property_tree/ptree.hpp>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/variant.h>

#include <functional>
#include <string>
#include <variant>

namespace vt = valhalla::tyr;
namespace nb = nanobind;
namespace {

using actor_action_t = std::string (vt::actor_t::*)(const std::string&,
                                                    const std::function<void()>*,
                                                    valhalla::Api*);
using actor_response_t = std::variant<nb::str, nb::bytes>;

actor_response_t call_actor(vt::actor_t& actor, const std::string& request, actor_action_t action) {
  std::string response;
  bool binary_response;
  {
    nb::gil_scoped_release release;
    valhalla::Api api;
    response = (actor.*action)(request, nullptr, &api);
    // PBF can clear Options; other serializers keep the normalized format.
    binary_response = !api.has_options() || api.options().format() == valhalla::Options::pbf ||
                      api.options().format() == valhalla::Options::geotiff;
  }
  if (binary_response) {
    return nb::bytes(response.data(), response.size());
  }
  return nb::str(response.data(), response.size());
}

// configuring multiple times is wasteful/ineffectual but not harmful
// TODO: make this threadsafe just in case its abused
const boost::property_tree::ptree configure(const std::string& config) {
  boost::property_tree::ptree pt;
  try {
    // parse the config and configure logging
    rapidjson::read_json(config, pt);

    valhalla::midgard::logging::ConfigureFromPtree(pt);
  } catch (...) { throw std::runtime_error("Failed to load config from: " + config); }

  return pt;
}
} // namespace

namespace pyvalhalla {

void init_actor(nb::module_& m) {
  // User-facing docstrings (class + each method) live on the Python `Actor`
  // wrapper in actor.py as string literals — Pyright/Pylance reads them
  // statically and does not walk the MRO, so the C++ docstrings would not
  // show up on hover in VSCode.
  nb::class_<vt::actor_t>(m, "_Actor")
      .def(
          "__init__",
          [](vt::actor_t* self, const std::string& config) {
            new (self) vt::actor_t(configure(config), true);
          },
          nb::arg("config"))
      .def("route", [](vt::actor_t& self,
                       std::string& req) { return call_actor(self, req, &vt::actor_t::route); })
      .def("locate", [](vt::actor_t& self,
                        std::string& req) { return call_actor(self, req, &vt::actor_t::locate); })
      .def("optimized_route",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::optimized_route);
           })
      .def("matrix", [](vt::actor_t& self,
                        std::string& req) { return call_actor(self, req, &vt::actor_t::matrix); })
      .def("sources_to_targets",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::sources_to_targets);
           })
      .def("isochrone",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::isochrone);
           })
      .def("trace_route",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::trace_route);
           })
      .def("trace_attributes",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::trace_attributes);
           })
      .def("height", [](vt::actor_t& self,
                        std::string& req) { return call_actor(self, req, &vt::actor_t::height); })
      .def("transit_available",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::transit_available);
           })
      .def("expansion",
           [](vt::actor_t& self, std::string& req) {
             return call_actor(self, req, &vt::actor_t::expansion);
           })
      .def("centroid", [](vt::actor_t& self,
                          std::string& req) { return call_actor(self, req, &vt::actor_t::centroid); })
      .def("status", [](vt::actor_t& self,
                        std::string& req) { return call_actor(self, req, &vt::actor_t::status); })
      .def("tile", [](vt::actor_t& self, std::string& req) -> nb::bytes {
        std::string result;
        {
          nb::gil_scoped_release release;
          result = self.tile(req);
        }
        return nb::bytes(result.c_str(), result.size());
      });
}
} // namespace pyvalhalla
