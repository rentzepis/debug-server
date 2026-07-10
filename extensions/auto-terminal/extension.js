const vscode = require("vscode");

function openTerminal() {
  if (!vscode.workspace.workspaceFolders?.length) {
    return false;
  }

  void vscode.commands.executeCommand("workbench.action.createTerminalEditor");
  return true;
}

exports.activate = function () {
  if (openTerminal()) {
    return;
  }

  const disposable = vscode.workspace.onDidChangeWorkspaceFolders(() => {
    if (openTerminal()) {
      disposable.dispose();
    }
  });
};
