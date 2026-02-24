import json
import unicodedata
import re
import os
import shutil

SOURCE_AUDIO_DIR = "audio/Italian"
DEST_AUDIO_DIR = "new_audio_Italian"

def remove_diacritics(text, diacritics_log):
    if not text:
        return text
    normalized = unicodedata.normalize('NFKD', text)
    result = "".join(c for c in normalized if not unicodedata.combining(c))
    if result != text:
        diacritics_log.append(("diacritics", text, result))
        print(f"[Diacritics Removed] '{text}' → '{result}'")
    return result

def replace_non_ascii_apostrophes(text, apostrophes_log):
    if not text:
        return text
    replacements = {
        '’': "'", '‘': "'", '‛': "'", '´': "'",
        '“': '"', '”': '"', '„': '"', '»': '"', '«': '"'
    }
    new_text = text
    for fancy, simple in replacements.items():
        if fancy in new_text:
            apostrophes_log.append(("apostrophe", text, fancy, simple))
            print(f"[Apostrophe Replaced] '{fancy}' → '{simple}' in '{text}'")
            new_text = new_text.replace(fancy, simple)
    return new_text

def restrict_to_printable_ascii(text):
    new_text = ""
    for ch in text:
        if 32 <= ord(ch) <= 126:
            new_text += ch
        else:
            print(f"[Removed Non-ASCII] '{ch}' in '{text}'")
    return new_text

def sanitize_filename(name):
    """Keep A-Z, a-z only; replace all else with '-'; preserve .wav at end."""
    if not name:
        return ""
    name = name.encode("ascii", errors="ignore").decode("ascii")
    cleaned = []
    for ch in name:
        if 65 <= ord(ch) <= 90 or 97 <= ord(ch) <= 122:
            cleaned.append(ch)
        else:
            print(f"[Filename Char Replaced] '{ch}' → '-' in '{name}'")
            cleaned.append('-')
    filename = ''.join(cleaned)
    filename = re.sub('-+', '-', filename)
    return filename.strip('-').lower()

def sanitize_text(text, diacritics_log, apostrophes_log):
    text = remove_diacritics(text, diacritics_log)
    text = replace_non_ascii_apostrophes(text, apostrophes_log)
    text = restrict_to_printable_ascii(text)
    return text

def format_cpp_string(s):
    """Escape quotes for C++ string literals."""
    return s.replace('"', '\\"')

def ensure_folder(path):
    if not os.path.exists(path):
        os.makedirs(path)

def copy_and_rename_audio(used_filenames, diacritics_log, apostrophes_log):
    """Copy every audio file from SOURCE_AUDIO_DIR → DEST_AUDIO_DIR with same sanitization rules."""
    ensure_folder(DEST_AUDIO_DIR)

    print("\n=== Processing Audio Files ===")

    # Map sanitized names for all files in source folder
    source_map = {}
    for src_file in os.listdir(SOURCE_AUDIO_DIR):
        if not src_file.lower().endswith(".wav"):
            continue

        base = os.path.splitext(src_file)[0]
        original_base = base
        base = sanitize_text(base, diacritics_log, apostrophes_log)
        base_sanitized = sanitize_filename(base)
        sanitized_name = base_sanitized + ".wav"
        source_map[sanitized_name] = os.path.join(SOURCE_AUDIO_DIR, src_file)

        dest_path = os.path.join(DEST_AUDIO_DIR, sanitized_name)
        print(f"[Copying Audio] '{src_file}' → '{sanitized_name}'")
        shutil.copy2(os.path.join(SOURCE_AUDIO_DIR, src_file), dest_path)

    # Verify that every cpp filename exists in destination audio folder
    dest_files = set(os.listdir(DEST_AUDIO_DIR))
    missing_cpp_audio = [fn for fn in used_filenames if fn not in dest_files]

    if missing_cpp_audio:
        print("\n⚠️ Missing audio files REFERENCED in Levels.cpp but not found:")
        for fn in missing_cpp_audio:
            print(f"  - {fn}")
    else:
        print("\n✅ All filenames referenced in Levels.cpp exist in new_audio_Italian.")

def generate_cpp(levels, diacritics_log, apostrophes_log):
    cpp_lines = []
    cpp_lines.append('#include "Levels.h"\n')
    cpp_lines.append("Level levels[] = {\n")

    used_audio_files = set()

    for level in levels:
        level_id = level["id"]
        title = format_cpp_string(sanitize_text(level["title"], diacritics_log, apostrophes_log))
        cpp_lines.append(f'  {{{level_id}, "{title}", {{\n')

        for w in level["words"]:
            word = format_cpp_string(sanitize_text(w["word"], diacritics_log, apostrophes_log))

            # Handle filename – must be ASCII only A–Z a–z and '-'
            filename_raw = sanitize_text(w.get("filename", ""), diacritics_log, apostrophes_log)
            filename_c = sanitize_filename(filename_raw) + ".wav"
            used_audio_files.add(filename_c)

            translations = w.get("translations", [])
            trans_cpp = []
            for i in range(3):
                if i < len(translations):
                    val = format_cpp_string(sanitize_text(translations[i], diacritics_log, apostrophes_log))
                    trans_cpp.append(f'"{val}"')
                else:
                    trans_cpp.append("NULL")

            definition = format_cpp_string(sanitize_text(w.get("definition", ""), diacritics_log, apostrophes_log))
            info_field = w.get("info")
            info = (
                f'"{format_cpp_string(sanitize_text(info_field, diacritics_log, apostrophes_log))}"'
                if info_field else "NULL"
            )

            cpp_lines.append(
                f'    {{"{word}", "{filename_c}", {{{", ".join(trans_cpp)}}}, "{definition}", {info}}},\n'
            )

        cpp_lines.append("  }},\n")

    cpp_lines.append("};\n")
    cpp_lines.append("const int numLevels = sizeof(levels) / sizeof(levels[0]);\n")

    return "".join(cpp_lines), used_audio_files

def main():
    input_file = "levels.json"
    output_file = "Levels.cpp"

    print("=== Starting Level Conversion ===")

    with open(input_file, "r", encoding="utf-8") as f:
        data = json.load(f)

    diacritics_log, apostrophes_log = [], []

    cpp_code, used_audio_files = generate_cpp(data["levels"], diacritics_log, apostrophes_log)

    cpp_ascii = cpp_code.encode("ascii", errors="ignore").decode("ascii")

    with open(output_file, "w", encoding="ascii") as f:
        f.write(cpp_ascii)

    print(f"\n✅ Generated ASCII-only {output_file}")

    copy_and_rename_audio(used_audio_files, diacritics_log, apostrophes_log)

    # Summary
    print("\n=== Summary ===")
    print(f"{len(diacritics_log)} diacritic removals, {len(apostrophes_log)} apostrophe/quote replacements.")
    print("Audio and level data fully synchronized.")
    print("=== Done ===")

if __name__ == "__main__":
    main()
