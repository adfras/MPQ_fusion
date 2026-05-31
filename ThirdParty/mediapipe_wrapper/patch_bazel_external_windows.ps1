param(
  [Parameter(Mandatory=$true)][string]$BazelOutputRoot
)

function Set-TextIfChanged {
  param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$Text
  )

  $Current = Get-Content -Raw -LiteralPath $Path
  if ($Current -ne $Text) {
    Set-Content -NoNewline -LiteralPath $Path -Value $Text
    Write-Host "Patched $Path"
  }
}

function Patch-ProtobufRoot {
  param([Parameter(Mandatory=$true)][string]$Root)

  $UntypedHeader = Join-Path $Root "src/google/protobuf/json/internal/untyped_message.h"
  if (Test-Path -LiteralPath $UntypedHeader) {
    $Text = Get-Content -Raw -LiteralPath $UntypedHeader
    if ($Text -notmatch '#include <memory>') {
      $Text = $Text -replace '(#include <cstdint>\r?\n)', "`$1#include <memory>`r`n"
    }
    if ($Text -notmatch 'using MessagePtr = std::unique_ptr<UntypedMessage>;') {
      $Text = $Text -replace 'enum Bool : unsigned char \{ kTrue, kFalse \};\s*using Value =', "enum Bool : unsigned char { kTrue, kFalse };`r`n  using MessagePtr = std::unique_ptr<UntypedMessage>;`r`n  using MessageList = std::vector<MessagePtr>;`r`n  using Value ="
    }
    $Text = $Text -replace 'double, std::string, UntypedMessage,', 'double, std::string, MessagePtr,'
    $Text = $Text -replace 'std::vector<double>, std::vector<std::string>,\s*std::vector<UntypedMessage>', "std::vector<double>, std::vector<std::string>,`r`n                             MessageList"
    if ($Text -notmatch 'const UntypedMessage\* GetMessage\(int32_t field_number') {
      $Method = @'

  const UntypedMessage* GetMessage(int32_t field_number, size_t idx = 0) const {
    auto it = fields_.find(field_number);
    if (it == fields_.end()) {
      return nullptr;
    }

    if (auto* val = std::get_if<MessagePtr>(&it->second)) {
      ABSL_CHECK(idx == 0);
      return val->get();
    } else if (auto* vec = std::get_if<MessageList>(&it->second)) {
      ABSL_CHECK(idx < vec->size());
      return (*vec)[idx].get();
    } else {
      ABSL_CHECK(false) << "wrong type for UntypedMessage::GetMessage("
                        << field_number << ")";
      return nullptr;
    }
  }
'@
      $Text = $Text -replace '(\r?\n  const ResolverPool::Message& desc\(\) const)', ($Method + '$1')
    }
    Set-TextIfChanged -Path $UntypedHeader -Text $Text
  }

  $UntypedSource = Join-Path $Root "src/google/protobuf/json/internal/untyped_message.cc"
  if (Test-Path -LiteralPath $UntypedSource) {
    $Text = Get-Content -Raw -LiteralPath $UntypedSource
    if ($Text -notmatch '#include <memory>') {
      $Text = $Text -replace '(#include <cstdint>\r?\n)', "`$1#include <memory>`r`n"
    }
    $Text = $Text -replace 'fields_\.try_emplace\(number, std::move\(value\)\)', 'fields_.try_emplace(number, std::make_unique<UntypedMessage>(std::move(value)))'
    $Text = $Text -replace 'std::get_if<UntypedMessage>\(&slot\)', 'std::get_if<MessagePtr>(&slot)'
    $Text = $Text -replace 'std::vector<UntypedMessage> repeated;', 'MessageList repeated;'
    $Text = $Text -replace 'repeated\.push_back\(std::move\(\*extant\)\);', 'repeated.push_back(std::move(*extant));'
    $Text = $Text -replace 'repeated\.push_back\(std::move\(value\)\);', 'repeated.push_back(std::make_unique<UntypedMessage>(std::move(value)));'
    $Text = $Text -replace 'std::get_if<std::vector<UntypedMessage>>\(&slot\)', 'std::get_if<MessageList>(&slot)'
    $Text = $Text -replace 'extant->push_back\(std::move\(value\)\);', 'extant->push_back(std::make_unique<UntypedMessage>(std::move(value)));'
    Set-TextIfChanged -Path $UntypedSource -Text $Text
  }

  $UnparserTraits = Join-Path $Root "src/google/protobuf/json/internal/unparser_traits.h"
  if (Test-Path -LiteralPath $UnparserTraits) {
    $Text = Get-Content -Raw -LiteralPath $UnparserTraits
    $Text = $Text -replace 'return &msg\.Get<Msg>\(f->proto\(\)\.number\(\)\)\[idx\];', 'return msg.GetMessage(f->proto().number(), idx);'
    Set-TextIfChanged -Path $UnparserTraits -Text $Text
  }

  $IoBuild = Join-Path $Root "src/google/protobuf/io/BUILD.bazel"
  if (Test-Path -LiteralPath $IoBuild) {
    $Text = Get-Content -Raw -LiteralPath $IoBuild
    $Start = $Text.IndexOf('name = "gzip_stream"')
    if ($Start -ge 0) {
      $BlockStart = $Text.LastIndexOf('cc_library(', $Start)
      $Next = $Text.IndexOf("cc_library(", $Start + 1)
      if ($Next -lt 0) {
        $Next = $Text.Length
      }
      $Block = $Text.Substring($BlockStart, $Next - $BlockStart)
      $NewBlock = $Block.Replace('copts = COPTS,', 'copts = COPTS + ["-DHAVE_ZLIB"],')
      if ($NewBlock -notmatch '"@zlib"') {
        $NewBlock = $NewBlock -replace '("@abseil-cpp//absl/log:absl_log",\r?\n)', ('$1        "@zlib",' + "`r`n")
      }
      if ($NewBlock -ne $Block) {
        $Text = $Text.Substring(0, $BlockStart) + $NewBlock + $Text.Substring($Next)
      }
      Set-TextIfChanged -Path $IoBuild -Text $Text
    }
  }
}

if (-not (Test-Path -LiteralPath $BazelOutputRoot)) {
  Write-Host "Bazel output root not found yet: $BazelOutputRoot"
  exit 0
}

$Candidates = @()
if ((Split-Path -Leaf $BazelOutputRoot) -eq 'protobuf~') {
  $Candidates += (Resolve-Path -LiteralPath $BazelOutputRoot).Path
} else {
  $DirectExternal = Join-Path $BazelOutputRoot "external/protobuf~"
  if (Test-Path -LiteralPath $DirectExternal) {
    $Candidates += (Resolve-Path -LiteralPath $DirectExternal).Path
  } else {
    $Candidates += Get-ChildItem -LiteralPath $BazelOutputRoot -Directory -ErrorAction SilentlyContinue |
      ForEach-Object {
        $Path = Join-Path $_.FullName "external/protobuf~"
        if (Test-Path -LiteralPath $Path) {
          (Resolve-Path -LiteralPath $Path).Path
        }
      }
  }
}

foreach ($Root in $Candidates) {
  if (Test-Path -LiteralPath (Join-Path $Root "src/google/protobuf/json/internal/untyped_message.h")) {
    Patch-ProtobufRoot -Root $Root
  }
}
