// -*- C++ -*-
//
// Package:     Services
// Class  :     edm::service::CPU
//
// Implementation:
//
// Original Author:  Natalia Garcia
// CPU.cc: v 1.0 2009/01/08 11:31:07

#include "FWCore/MessageLogger/interface/JobReport.h"
#include "FWCore/ServiceRegistry/interface/ActivityRegistry.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/CPUServiceBase.h"
#include "FWCore/Utilities/interface/ResourceInformation.h"

#include "cpu_features/cpu_features_macros.h"

#if defined(CPU_FEATURES_ARCH_X86)
#include "cpu_features/cpuinfo_x86.h"
#elif defined(CPU_FEATURES_ARCH_ARM)
#include "cpu_features/cpuinfo_arm.h"
#elif defined(CPU_FEATURES_ARCH_AARCH64)
#include "cpu_features/cpuinfo_aarch64.h"
#elif defined(CPU_FEATURES_ARCH_PPC)
#include "cpu_features/cpuinfo_ppc.h"
#endif

#include <cstdlib>
#include <string>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include <fmt/format.h>

#ifdef __linux__
#include <sched.h>
#include <cerrno>
#endif

namespace edm {

  namespace service {
    class CPU : public CPUServiceBase {
    public:
      CPU(ParameterSet const &, ActivityRegistry &);
      ~CPU() override;

      static void fillDescriptions(ConfigurationDescriptions &descriptions);

    private:
      const bool reportCPUProperties_;
      const bool disableJobReportOutput_;

      bool parseCPUInfo(std::vector<std::pair<std::string, std::string>> &info);
      std::string getModels(const std::vector<std::pair<std::string, std::string>> &info);
      std::string getModelFromCPUFeatures();
      double getAverageSpeed(const std::vector<std::pair<std::string, std::string>> &info);
      void postEndJob();
    };

    inline bool isProcessWideService(CPU const *) { return true; }
  }  // namespace service
}  // namespace edm

namespace edm {
  namespace service {
    namespace {

      std::string i2str(int i) {
        std::ostringstream t;
        t << i;
        return t.str();
      }

      std::string d2str(double d) {
        std::ostringstream t;
        t << d;
        return t.str();
      }

      double str2d(std::string s) { return atof(s.c_str()); }

      void trim(std::string &s, const std::string &drop = " \t") {
        std::string::size_type p = s.find_last_not_of(drop);
        if (p != std::string::npos) {
          s = s.erase(p + 1);
        }
        s = s.erase(0, s.find_first_not_of(drop));
      }

      std::string eraseExtraSpaces(std::string s) {
        bool founded = false;
        std::string aux;
        for (std::string::const_iterator iter = s.begin(); iter != s.end(); iter++) {
          if (founded) {
            if (*iter == ' ')
              founded = true;
            else {
              aux += " ";
              aux += *iter;
              founded = false;
            }
          } else {
            if (*iter == ' ')
              founded = true;
            else
              aux += *iter;
          }
        }
        return aux;
      }

      // Determine the CPU set size; if this can be successfully determined, then this
      // returns true.
      bool getCpuSetSize(unsigned &set_size) {
#ifdef __linux__
        cpu_set_t *cpusetp;
        unsigned current_size = 128;
        unsigned cpu_count = 0;
        while (current_size * 2 > current_size) {
          cpusetp = CPU_ALLOC(current_size);
          CPU_ZERO_S(CPU_ALLOC_SIZE(current_size), cpusetp);

          if (sched_getaffinity(0, CPU_ALLOC_SIZE(current_size), cpusetp)) {
            CPU_FREE(cpusetp);
            if (errno == EINVAL) {
              current_size *= 2;
              continue;
            }
            return false;
          }
          cpu_count = CPU_COUNT_S(CPU_ALLOC_SIZE(current_size), cpusetp);
          CPU_FREE(cpusetp);
          break;
        }
        set_size = cpu_count;
        return true;
#else
        return false;
#endif
      }
    }  // namespace

    CPU::CPU(const ParameterSet &iPS, ActivityRegistry &iRegistry)
        : reportCPUProperties_(iPS.getUntrackedParameter<bool>("reportCPUProperties")),
          disableJobReportOutput_(iPS.getUntrackedParameter<bool>("disableJobReportOutput")) {
      edm::Service<edm::ResourceInformation> resourceInformationService;
      if (resourceInformationService.isAvailable()) {
        std::vector<std::pair<std::string, std::string>> info;
        if (parseCPUInfo(info)) {
          std::string models = getModels(info);
          double averageSpeed = getAverageSpeed(info);
          resourceInformationService->setCpuModelsFormatted(models);
          resourceInformationService->setCpuAverageSpeed(averageSpeed);

          std::set<std::string> modelsSet;
          for (const auto &entry : info) {
            if (entry.first == "model name") {
              modelsSet.insert(entry.second);
            }
          }
          std::vector<std::string> modelsVector(modelsSet.begin(), modelsSet.end());
          resourceInformationService->setCPUModels(modelsVector);
        }
      }
      iRegistry.watchPostEndJob(this, &CPU::postEndJob);
    }

    CPU::~CPU() {}

    void CPU::fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
      edm::ParameterSetDescription desc;
      desc.addUntracked<bool>("reportCPUProperties", false);
      desc.addUntracked<bool>("disableJobReportOutput", false);
      descriptions.add("CPU", desc);
    }

    void CPU::postEndJob() {
      if (disableJobReportOutput_) {
        return;
      }

      Service<JobReport> reportSvc;

      std::vector<std::pair<std::string, std::string>> info;
      if (!parseCPUInfo(info)) {
        return;
      }

      std::string models = getModels(info);
      double avgSpeed = getAverageSpeed(info);
      unsigned totalNumberCPUs = 0;
      std::map<std::string, std::string> currentCoreProperties;
      std::string currentCore;

      for (const auto &entry : info) {
        if (entry.first == "processor") {
          if (reportCPUProperties_) {
            if (currentCore.empty()) {  // first core
              currentCore = entry.second;
            } else {
              reportSvc->reportPerformanceForModule("SystemCPU", "CPU-" + currentCore, currentCoreProperties);
              currentCoreProperties.clear();
              currentCore = entry.second;
            }
          }
          totalNumberCPUs++;
        } else if (reportCPUProperties_) {
          currentCoreProperties.insert(entry);
        }
      }
      if (!currentCore.empty() && reportCPUProperties_) {
        reportSvc->reportPerformanceForModule("SystemCPU", "CPU-" + currentCore, currentCoreProperties);
      }

      std::map<std::string, std::string> reportCPUProperties{
          {"totalCPUs", i2str(totalNumberCPUs)}, {"averageCoreSpeed", d2str(avgSpeed)}, {"CPUModels", models}};
      unsigned set_size = -1;
      if (getCpuSetSize(set_size)) {
        reportCPUProperties.insert(std::make_pair("cpusetCount", i2str(set_size)));
      }
      reportSvc->reportPerformanceSummary("SystemCPU", reportCPUProperties);
    }

    bool CPU::parseCPUInfo(std::vector<std::pair<std::string, std::string>> &info) {
      info.clear();
      std::ifstream fcpuinfo("/proc/cpuinfo");
      if (!fcpuinfo.is_open()) {
        return false;
      }
      while (!fcpuinfo.eof()) {
        std::string buf;
        std::getline(fcpuinfo, buf);

        std::istringstream iss(buf);
        std::string token;
        std::string property;
        std::string value;

        int time = 1;

        while (std::getline(iss, token, ':')) {
          switch (time) {
            case 1:
              property = token;
              break;
            case 2:
              value = token;
              break;
            default:
              value += token;
              break;
          }
          time++;
        }
        trim(property);
        trim(value);
        if (property.empty()) {
          continue;
        }

        if (property == "model name") {
          value = eraseExtraSpaces(value);
        }
        info.emplace_back(property, value);
      }
      return true;
    }

    std::string CPU::getModelFromCPUFeatures() {
      using namespace cpu_features;

      std::string model;
#if defined(CPU_FEATURES_ARCH_X86)
      const auto info{GetX86Info()};
      model = info.brand_string;
#elif defined(CPU_FEATURES_ARCH_ARM)
      const auto info{GetArmInfo()};
      model = fmt::format("ARM {} {} {}", info.implementer, info.architecture, info.variant);
#elif defined(CPU_FEATURES_ARCH_AARCH64)
      const auto info{GetAarch64Info()};
      model = fmt::format("aarch64 {} {}", info.implementer, info.variant);
#elif defined(CPU_FEATURES_ARCH_PPC)
      const auto strings{GetPPCPlatformStrings()};
      model = strings.machine;
#endif
      return model;
    }

    std::string CPU::getModels(const std::vector<std::pair<std::string, std::string>> &info) {
      std::set<std::string> models;
      for (const auto &entry : info) {
        if (entry.first == "model name") {
          models.insert(entry.second);
        }
      }
      std::stringstream ss;
      int model = 0;
      for (const auto &modelname : models) {
        if (model++ != 0) {
          ss << ", ";
        }
        ss << modelname;
      }
      // If "model name" isn't present in /proc/cpuinfo, see what we can get
      // from cpu_features
      if (0 == model) {
        return getModelFromCPUFeatures();
      }
      return ss.str();
    }

    double CPU::getAverageSpeed(const std::vector<std::pair<std::string, std::string>> &info) {
      double averageCoreSpeed = 0.0;
      unsigned coreCount = 0;
      for (const auto &entry : info) {
        if (entry.first == "cpu MHz") {
          averageCoreSpeed += str2d(entry.second);
          coreCount++;
        }
      }
      if (!coreCount) {
        return 0;
      }
      return averageCoreSpeed / static_cast<double>(coreCount);
    }
  }  // namespace service
}  // namespace edm

#include "FWCore/ServiceRegistry/interface/ServiceMaker.h"

using edm::service::CPU;
typedef edm::serviceregistry::AllArgsMaker<edm::CPUServiceBase, CPU> CPUMaker;
DEFINE_FWK_SERVICE_MAKER(CPU, CPUMaker);
