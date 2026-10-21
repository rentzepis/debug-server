from dataclasses import dataclass
from enum import StrEnum
from huggingface_hub import hf_hub_download
from llama_cpp import Llama

PREFIX_SEP = "|"

@dataclass
class Chunk:
    match_line: int      # 1-indexed line number of the regex match
    start_line: int      # 1-indexed, inclusive
    end_line: int        # 1-indexed, inclusive
    lines: list          # raw source lines, no trailing newline

    def numbered(self, sep: str = PREFIX_SEP) -> str:
        """Render as 'NNN|<line>' (original indentation kept after the pipe)."""
        return "\n".join(
            f"{self.start_line + i}{sep}{line}" for i, line in enumerate(self.lines)
        )
    
class MutationType(StrEnum):
    PROTOCOL = "protocol"
    ROBUSTIO = "robustio"
    BUFLEN = "buflen"
    PARSING = "parsing"

def load_model(model, filename):
    model_path = hf_hub_download(
        repo_id=model,
        filename=filename
    )

    llm = Llama(
        model_path=model_path,
        n_gpu_layers=-1,
        n_ctx=8192,
        n_threads=4,
        device="auto",
        verbose=True
    )
    return llm