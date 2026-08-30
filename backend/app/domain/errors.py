class ApplicationError(RuntimeError):
    """Base error for use-case failures safe to map at the API boundary."""


class ScenarioNotFound(ApplicationError):
    pass


class ScenarioValidationFailed(ApplicationError):
    pass


class RuntimeRevisionNotFound(ApplicationError):
    pass


class RuntimeUnavailable(ApplicationError):
    pass


class BuildUnavailable(ApplicationError):
    pass


class SnapshotWriteFailed(ApplicationError):
    pass
