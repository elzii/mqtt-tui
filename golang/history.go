package main

import (
	"encoding/csv"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

var (
	historyMutex sync.Mutex
	historyFile  string
)

func init() {
	home, err := os.UserHomeDir()
	if err != nil {
		home = "."
	}
	historyFile = filepath.Join(home, ".config", "mqtttui", "history.csv")
}

func ensureHistoryFileExists() error {
	dir := filepath.Dir(historyFile)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}
	// If accidentally a directory, remove it
	info, err := os.Stat(historyFile)
	if err == nil && info.IsDir() {
		_ = os.Remove(historyFile)
	}
	if _, err := os.Stat(historyFile); os.IsNotExist(err) {
		f, err := os.Create(historyFile)
		if err != nil {
			return err
		}
		defer f.Close()
		w := csv.NewWriter(f)
		_ = w.Write([]string{"timestamp", "field", "value"})
		w.Flush()
	}
	return nil
}

// LoadHistory loads all past values for a field (oldest to newest)
func LoadHistory(field string) []string {
	historyMutex.Lock()
	defer historyMutex.Unlock()

	_ = ensureHistoryFileExists()
	f, err := os.Open(historyFile)
	if err != nil {
		return nil
	}
	defer f.Close()

	r := csv.NewReader(f)
	records, err := r.ReadAll()
	if err != nil {
		return nil
	}

	var results []string
	for i, row := range records {
		if i == 0 || len(row) < 3 {
			continue
		}
		if row[1] == field && strings.TrimSpace(row[2]) != "" {
			results = append(results, row[2])
		}
	}
	return results
}

// SaveHistory writes a new entry for a field into history.csv
func SaveHistory(field, value string) {
	val := strings.TrimSpace(value)
	if val == "" {
		return
	}

	historyMutex.Lock()
	defer historyMutex.Unlock()

	_ = ensureHistoryFileExists()
	f, err := os.OpenFile(historyFile, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0644)
	if err != nil {
		return
	}
	defer f.Close()

	w := csv.NewWriter(f)
	now := time.Now().Format(time.RFC3339)
	_ = w.Write([]string{now, field, val})
	w.Flush()
}
