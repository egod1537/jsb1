"""Compatibility import for the filesystem scenario source adapter."""

from app.infrastructure.filesystem.scenarios import DirectoryScenarioSource

BundledScenarioSource = DirectoryScenarioSource

__all__ = ["BundledScenarioSource"]
