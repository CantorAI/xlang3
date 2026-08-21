/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
const vscode = require("vscode");

function adapterPathFromConfig(session) {
  if (session.configuration && session.configuration.adapterPath) {
    return session.configuration.adapterPath;
  }

  const configured = vscode.workspace.getConfiguration("xlang3").get("debugAdapterPath");
  if (configured) {
    return configured;
  }

  if (process.env.XLANG3_EXE) {
    return process.env.XLANG3_EXE;
  }

  return "xlang3";
}

function activate(context) {
  const factory = vscode.debug.registerDebugAdapterDescriptorFactory("xlang3", {
    createDebugAdapterDescriptor(session) {
      return new vscode.DebugAdapterExecutable(adapterPathFromConfig(session), ["--dap-stdio"]);
    },
  });

  context.subscriptions.push(factory);
}

function deactivate() {}

module.exports = {
  activate,
  deactivate,
};
