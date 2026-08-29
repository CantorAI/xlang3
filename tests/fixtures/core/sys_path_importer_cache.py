# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import sys
import zipfile
import zipimport

zip_path = "xlang3_path_importer_cache_fixture.zip"
module_name = "xlang3_path_importer_cache_fixture_mod"
package_name = "xlang3_path_importer_cache_fixture_pkg"
try:
    with zipfile.ZipFile(zip_path, "w") as zf:
        zf.writestr(module_name + ".py", "VALUE = 314\n")
        zf.writestr(package_name + "/__init__.py", "NAME = 'pkg'\n")
    sys.path.insert(0, zip_path)
    module = __import__(module_name)
    importer = sys.path_importer_cache.get(zip_path)
    module_spec = importer.find_spec(module_name)
    package_spec = importer.find_spec(package_name)
    print(
        "path-importer-cache",
        module.VALUE,
        zip_path in sys.path_importer_cache,
        type(importer) is zipimport.zipimporter,
        getattr(importer, "archive", None) == zip_path,
        getattr(importer, "prefix", None) == "",
    )
    print(
        "path-importer-cache-spec",
        module_spec.name,
        module_spec.loader is importer,
        module_spec.origin.endswith(module_name + ".py"),
        module_spec.submodule_search_locations is None,
        importer.get_filename(module_name).endswith(module_name + ".py"),
        importer.get_source(module_name) == "VALUE = 314\n",
        importer.is_package(module_name),
    )
    print(
        "path-importer-cache-package-spec",
        package_spec.name,
        package_spec.loader is importer,
        package_spec.origin.endswith("__init__.py"),
        package_spec.submodule_search_locations[0].endswith(package_name),
        importer.get_source(package_name) == "NAME = 'pkg'\n",
        importer.is_package(package_name),
        importer.find_spec("xlang3_path_importer_cache_fixture_missing") is None,
    )
finally:
    if sys.path and sys.path[0] == zip_path:
        sys.path.pop(0)
    if zip_path in sys.path_importer_cache:
        del sys.path_importer_cache[zip_path]
    if module_name in sys.modules:
        del sys.modules[module_name]
    if package_name in sys.modules:
        del sys.modules[package_name]
    if os.path.exists(zip_path):
        os.remove(zip_path)
