package main

import (
	"bytes"
	"encoding/json"
	"strings"
)

// BeautifyJSON parses lines containing JSON objects and indents them into multiline JSON
func BeautifyJSON(rawText string) string {
	if strings.TrimSpace(rawText) == "" {
		return rawText
	}

	lines := strings.Split(rawText, "\n")
	var newLines []string
	modified := false

	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if trimmed == "" {
			newLines = append(newLines, line)
			continue
		}

		prefix := ""
		jsonCandidate := ""

		// Check if line starts with timestamp like [11:34:50]
		if strings.HasPrefix(line, "[") && strings.Contains(line, "]") {
			cb := strings.Index(line, "]")
			inside := line[1:cb]
			if strings.Contains(inside, ":") {
				prefix = strings.TrimSpace(line[:cb+1])
				rem := strings.TrimSpace(line[cb+1:])
				fb := strings.Index(rem, "{")
				if fb != -1 {
					topicPart := strings.TrimSpace(rem[:fb])
					if topicPart != "" {
						prefix = prefix + " " + topicPart
					}
					jsonCandidate = strings.TrimSpace(rem[fb:])
				} else {
					jsonCandidate = rem
				}
			}
		}

		if jsonCandidate == "" {
			fb := strings.Index(line, "{")
			if fb != -1 {
				prefix = strings.TrimSpace(line[:fb])
				jsonCandidate = strings.TrimSpace(line[fb:])
			}
		}

		if jsonCandidate != "" && strings.HasPrefix(jsonCandidate, "{") && strings.HasSuffix(jsonCandidate, "}") {
			var parsed map[string]interface{}
			if err := json.Unmarshal([]byte(jsonCandidate), &parsed); err == nil {
				var out bytes.Buffer
				enc := json.NewEncoder(&out)
				enc.SetIndent("", "  ")
				if err := enc.Encode(parsed); err == nil {
					pretty := strings.TrimRight(out.String(), "\n")
					if prefix != "" {
						newLines = append(newLines, prefix)
					}
					newLines = append(newLines, strings.Split(pretty, "\n")...)
					modified = true
					continue
				}
			}
		}

		newLines = append(newLines, line)
	}

	if !modified {
		return rawText
	}
	return strings.Join(newLines, "\n")
}
