package utils

import "testing"

func TestGetLargestVersion(t *testing.T) {
	tests := []struct {
		name     string
		versions []string
		want     string
	}{
		{
			name:     "empty versions",
			versions: []string{},
			want:     "",
		},
		{
			name:     "single version",
			versions: []string{"1.0.0"},
			want:     "1.0.0",
		},
		{
			name:     "short versions",
			versions: []string{"1.0", "1.1", "1.2"},
			want:     "1.2",
		},
		{
			name:     "mixed short versions",
			versions: []string{"1", "1.1.0", "1.2", "2"},
			want:     "2",
		},
		{
			name:     "basic versions",
			versions: []string{"1.0.0", "1.0.1", "1.1.0"},
			want:     "1.1.0",
		},
		{
			name:     "prerelease versions",
			versions: []string{"1.0.0-alpha.1", "1.0.0-alpha.2", "1.0.0-alpha.3"},
			want:     "1.0.0-alpha.3",
		},
		{
			name:     "mixed versions",
			versions: []string{"1.0.0-alpha.1", "1.0.0", "1.0.1"},
			want:     "1.0.1",
		},
		{
			name:     "release vs prerelease",
			versions: []string{"1.0.0-beta.1", "1.0.0"},
			want:     "1.0.0",
		},
		{
			name:     "complex versions",
			versions: []string{"2.0.0", "1.9.9", "1.10.0", "1.9.10"},
			want:     "2.0.0",
		},
		{
			name:     "single version",
			versions: []string{"1.0.0"},
			want:     "1.0.0",
		},
		{
			name:     "empty versions",
			versions: []string{},
			want:     "",
		},
		{
			name:     "different prerelease identifiers",
			versions: []string{"1.0.0-alpha.1", "1.0.0-beta.1", "1.0.0-rc.1"},
			want:     "1.0.0-rc.1",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if got := GetLargestVersion(tt.versions); got != tt.want {
				t.Errorf("GetLargestVersion() = %v, want %v", got, tt.want)
			}
		})
	}
}
