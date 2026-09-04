from __future__ import annotations

from app.domain.errors import NotFound
from app.domain.models import Comparison
from app.repositories.comparisons import ComparisonRepository


class ComparisonNotFound(NotFound):
    pass


class ComparisonQueryService:
    def __init__(self, comparisons: ComparisonRepository) -> None:
        self.comparisons = comparisons

    def list(self, limit: int) -> list[Comparison]:
        return self.comparisons.list(limit)

    def require(self, comparison_id: int) -> Comparison:
        try:
            return self.comparisons.get(comparison_id)
        except KeyError as exc:
            raise ComparisonNotFound(
                f"comparison not found: {comparison_id}"
            ) from exc
