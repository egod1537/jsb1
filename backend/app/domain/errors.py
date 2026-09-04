class ApplicationError(RuntimeError):
    """Base error for use-case failures safe to map at the API boundary."""


class NotFound(ApplicationError):
    pass


class Conflict(ApplicationError):
    pass


class RepositoryConflict(Conflict):
    pass


class DeploymentConflict(Conflict):
    pass


class NoDeploymentPortAvailable(Conflict):
    pass


class UnsafePath(ApplicationError, ValueError):
    pass


class InvalidTransition(ApplicationError):
    pass


class InvalidStatusTransition(InvalidTransition):
    pass


class InvalidDeploymentTransition(InvalidTransition):
    pass


class ContractCompatibilityError(ApplicationError):
    pass


class ExternalProcessError(ApplicationError):
    pass


class ExternalProcessUnavailable(ExternalProcessError):
    pass


class ExternalProcessTimedOut(ExternalProcessError):
    pass


class DeploymentOperationError(ApplicationError):
    pass


class DeploymentConfigurationError(DeploymentOperationError):
    pass


class ScenarioNotFound(NotFound):
    pass


class ScenarioValidationFailed(ApplicationError):
    pass


class RuntimeRevisionNotFound(NotFound):
    pass


class RuntimeUnavailable(ExternalProcessError):
    pass


class BuildUnavailable(ExternalProcessError):
    pass


class SnapshotWriteFailed(ApplicationError):
    pass


class RuntimeArtifactError(ApplicationError):
    pass


class RuntimeReportedFailure(ApplicationError):
    pass


class RunAnalysisError(ApplicationError):
    pass
